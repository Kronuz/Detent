#include "detent_local/client_source.hh"
#include "detent/detail/evaluation.hh"
#include "detent/validation.hh"
#include "resources.hh"
#include <atomic_shared_ptr.h>
#include <condition_variable>
#include <thread>

namespace detent::local {
namespace {
template <class T> Result<T, KnobError> qualified(Result<T, KnobError> result, std::string_view name) {
	if (result)
		return result;
	auto error = std::move(result).error();
	error.name.assign(name);
	return std::move(error);
}
struct FrozenEvaluation : distribution::RetirementNode {
	ReadView live;
	std::shared_ptr<const Snapshot> defaults;
	std::shared_ptr<const OverrideTable> overrides;
	ClientReadView fallback;
	ReadSourceStatus metadata;
	FrozenEvaluation(ReadView view, std::shared_ptr<const Snapshot> baked,
					 std::shared_ptr<const OverrideTable> cli)
		: live(std::move(view)), defaults(std::move(baked)), overrides(std::move(cli)), fallback(defaults) {
		metadata.last_success = std::chrono::steady_clock::now();
		metadata.bool_knobs = live.bool_count();
		metadata.int_knobs = live.int_count();
		if (live)
			metadata.cursor = live.cursor();
		if (defaults) {
			for (const auto &[name, definition] : defaults->knobs) {
				auto separator = name.find(':');
				if (!live.contains(name.substr(0, separator), name.substr(separator + 1))) {
					metadata.bool_knobs += is_bool(definition);
					metadata.int_knobs += is_int(definition);
				}
			}
		}
	}
	Result<bool, KnobError> eval(std::string_view name, std::optional<std::string_view> hash,
								 std::optional<std::string_view> group) const {
		if (auto override = detent::detail::override_value<bool>(overrides.get(), name))
			return std::move(*override);
		auto separator = name.find(':');
		if (auto value = live.try_eval(name.substr(0, separator), name.substr(separator + 1), hash, group))
			return qualified(std::move(*value), name);
		return fallback.eval(name, hash, group);
	}
	Result<std::int64_t, KnobError> get(std::string_view name) const {
		if (auto override = detent::detail::override_value<std::int64_t>(overrides.get(), name))
			return std::move(*override);
		auto separator = name.find(':');
		if (auto value = live.try_get(name.substr(0, separator), name.substr(separator + 1)))
			return qualified(std::move(*value), name);
		return fallback.get(name);
	}
};
class LocalEvaluation final : public EvaluationView {
  public:
	explicit LocalEvaluation(distribution::RetirementHandle<FrozenEvaluation> frozen)
		: frozen_(std::move(frozen)) {}
	Result<bool, KnobError> eval(std::string_view name, std::optional<std::string_view> hash,
								 std::optional<std::string_view> group) const override {
		return frozen_->eval(name, hash, group);
	}
	Result<std::int64_t, KnobError> get(std::string_view name) const override { return frozen_->get(name); }
	std::optional<distribution::Cursor> cursor() const noexcept override { return frozen_->metadata.cursor; }
	const ReadSourceStatus &metadata() const noexcept { return frozen_->metadata; }

  private:
	distribution::RetirementHandle<FrozenEvaluation> frozen_;
};
} // namespace
struct LocalReadSource::Impl {
	ClientSourceOptions options;
	GenerationReader reader;
	std::shared_ptr<const Snapshot> defaults;
	std::shared_ptr<const OverrideTable> overrides;
	distribution::RetirementDomain &domain;
	atomic_shared_ptr<const LocalEvaluation> installed;
	std::optional<Descriptor> installed_descriptor;
	std::mutex refresh_mutex, sleep_mutex;
	std::condition_variable_any wake;
	std::atomic<bool> connected{false};
	std::optional<std::jthread> worker;
	Impl(ClientSourceOptions config, std::shared_ptr<const Snapshot> baked,
		 std::shared_ptr<const OverrideTable> cli)
		: options(std::move(config)), reader(options.directory, options.limits, options.identity),
		  defaults(std::move(baked)), overrides(std::move(cli)), domain(resources::domain(options.limits)) {
		install({});
	}
	void install(ReadView live) {
		auto frozen = domain.make<FrozenEvaluation>(std::move(live), defaults, overrides);
		auto evaluator = std::allocate_shared<LocalEvaluation>(
			distribution::StorageAllocator<LocalEvaluation>(domain.storage_account()), std::move(frozen));
		installed.store(std::move(evaluator));
	}
};
LocalReadSource::LocalReadSource(ClientSourceOptions options, std::shared_ptr<const Snapshot> defaults,
								 std::shared_ptr<const OverrideTable> overrides) {
	if (options.directory.empty() || options.poll_interval <= std::chrono::milliseconds::zero() ||
		options.poll_interval > std::chrono::hours(24))
		throw std::invalid_argument("invalid local reader configuration");
	auto account = resources::domain(options.limits).storage_account();
	impl_ = distribution::make_storage_owner<Impl>(account, std::malloc, std::free, std::move(options),
												   std::move(defaults), std::move(overrides));
	if (impl_->options.auto_refresh) {
		impl_->worker.emplace([this](std::stop_token stop) {
			while (!stop.stop_requested()) {
				try {
					refresh();
				} catch (...) {
				}
				std::unique_lock lock(impl_->sleep_mutex);
				impl_->wake.wait_for(lock, stop, impl_->options.poll_interval, [] { return false; });
			}
		});
	}
}
LocalReadSource::~LocalReadSource() {
	if (impl_->worker) {
		impl_->worker->request_stop();
		impl_->wake.notify_all();
		impl_->worker->join();
	}
}
ClientReadView LocalReadSource::read_view() const noexcept { return ClientReadView(impl_->installed.load()); }
ReadSourceStatus LocalReadSource::status() const noexcept {
	auto status = impl_->installed.load()->metadata();
	status.connected = impl_->connected.load(std::memory_order_relaxed);
	return status;
}
bool LocalReadSource::refresh() {
	std::lock_guard lock(impl_->refresh_mutex);
	try {
		impl_->reader.refresh();
		auto descriptor = impl_->reader.descriptor();
		bool changed = descriptor != impl_->installed_descriptor;
		if (changed) {
			impl_->install(impl_->reader.read_view());
			impl_->installed_descriptor = descriptor;
		}
		impl_->connected.store(true, std::memory_order_relaxed);
		return changed;
	} catch (...) {
		impl_->connected.store(false, std::memory_order_relaxed);
		throw;
	}
}
ReadSourceFactory make_local_read_source(ClientSourceOptions options) {
	return [options = std::move(options)](std::shared_ptr<const Snapshot> defaults,
										  std::shared_ptr<const OverrideTable> overrides) {
		auto account = resources::domain(options.limits).storage_account();
		return std::allocate_shared<LocalReadSource>(distribution::StorageAllocator<LocalReadSource>(account),
													 options, std::move(defaults), std::move(overrides));
	};
}
} // namespace detent::local
