#include "detent_local/collector.hh"
#include <chrono>
#include <completion/image.h>
#include <completion/native_completion.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
namespace l = detent::local;
namespace c = kronuz::io::completion;
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("session invariant failed at line " + std::to_string(where.line()));
}
template <class Predicate>
void eventually(Predicate predicate, std::source_location where = std::source_location::current()) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!predicate()) {
		if (std::chrono::steady_clock::now() > deadline)
			throw std::runtime_error("session retirement timeout at line " + std::to_string(where.line()));
		std::this_thread::yield();
	}
}
template <class Action> void rejects(Action action) {
	bool rejected = false;
	try {
		action();
	} catch (const std::exception &) {
		rejected = true;
	}
	check(rejected);
}
struct Observation {
	d::RetirementRuntime &runtime;
	std::atomic<bool> retired{false}, wrong_thread{false}, open_directory{false};
	int directory = -1;
};
struct Admission {
	Observation &observation;
	~Admission() {
		observation.wrong_thread = !observation.runtime.on_worker();
		observation.open_directory =
			observation.directory >= 0 && ::fcntl(observation.directory, F_GETFD) >= 0;
		observation.retired.store(true, std::memory_order_release);
	}
};
struct Pause {
	std::atomic<bool> entered{false}, release{false};
	static void hook(void *value) noexcept {
		auto &pause = *static_cast<Pause *>(value);
		pause.entered.store(true, std::memory_order_release);
		while (!pause.release.load(std::memory_order_acquire))
			std::this_thread::yield();
	}
	~Pause() { release.store(true, std::memory_order_release); }
};
void drive(c::Operation &job) {
	while (!job.done()) {
		job.submitted();
		check(job.complete(kronuz::journal::detail::execute_primitive(job.io(), job.request())));
	}
}
} // namespace
int main() {
	try {
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("session-test-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto baseline = std::make_shared<d::StorageLedger>(1024 * 1024);
		auto account = std::make_shared<d::StorageLedger>(65536);
		d::RetirementRuntime runtime(baseline, 4);
		std::size_t bootstrap_capacity = 0;
		{
			auto domain = runtime.domain(account, 1);
			const auto domain_capacity = account->used();
			Observation observed{runtime};
			auto session = l::make_owned_storage_session(
				domain, directory, std::shared_ptr<Admission>(new Admission{observed}), true);
			auto alias = session->backend();
			observed.directory = dynamic_cast<c::PosixIO *>(alias.get())->native_directory_handle();
			const auto capacity = account->used();
			bootstrap_capacity = capacity - domain_capacity;
			check(capacity > 0 && domain.residents() == 1);
			std::weak_ptr<c::IO> weak = alias;
			auto cast = std::dynamic_pointer_cast<c::PosixIO>(alias);
			auto controls = l::make_collector_controls(session);
			auto claim = controls->claim(l::CollectorUse::Census);
			auto bytes = std::make_shared<const std::string>("prepared");
			auto prep = c::make_image_preparation(session->allocation_context(), alias, session,
												  session->next_operation(), "prepared.image", bytes, 64,
												  c::PreparationFilePolicy::CloseAfterSeal);
			drive(*prep);
			check(!prep->error());
			std::optional<c::PreparedImage> prepared = prep->prepared();
			prep.reset();
			session->close();
			rejects([&] { session->next_operation(); });
			rejects([&] { l::make_collector_controls(session); });
			controls->release(claim); // Existing cleanup remains authorized.
			rejects([&] { controls->claim(l::CollectorUse::Reclaim); });
			auto drain = domain.close();
			session.reset();
			controls.reset();
			alias.reset();
			cast.reset();
			check(!drain.done() && !observed.retired && weak.lock());
			// Prepared state destroys its last session lease before its IO alias.
			// A concurrent copy still pins the same native owner control group.
			auto survivor = weak.lock();
			prepared.reset();
			check(!drain.done() && !observed.retired);
			auto saturated = account->admit(account->limit() - account->used());
			survivor.reset();
			drain.wait();
			check(observed.retired && !observed.wrong_thread && !observed.open_directory);
			check(weak.expired() && account->used() > saturated.bytes() + domain_capacity);
			saturated.reset();
			weak.reset();
			eventually([&] { return account->used() == domain_capacity; });
			std::cout << "owning session capacity: " << bootstrap_capacity << " admitted bytes\n";
		}
		{
			auto domain = runtime.domain(account, 1);
			Observation observed{runtime};
			Pause pause;
			d::detail::RetirementTesting::before_ready(domain, Pause::hook, &pause);
			auto session = l::make_owned_storage_session(
				domain, directory, std::shared_ptr<Admission>(new Admission{observed}), false);
			observed.directory =
				dynamic_cast<c::PosixIO *>(session->backend().get())->native_directory_handle();
			auto alias = session->backend();
			auto drain = domain.close();
			session.reset();
			std::jthread releaser([held = std::move(alias)]() mutable { held.reset(); });
			struct Unpause {
				Pause &pause;
				~Unpause() { pause.release.store(true, std::memory_order_release); }
			} unpause{pause};
			eventually([&] { return pause.entered.load(std::memory_order_acquire); });
			check(!drain.done() && !observed.retired);
			pause.release.store(true, std::memory_order_release);
			releaser.join();
			drain.wait();
			check(observed.retired && !observed.wrong_thread && !observed.open_directory);
		}
		eventually([&] { return account->used() == 0; });
		{
			auto domain = runtime.domain(account, 1);
			Observation observed{runtime};
			rejects([&] {
				l::make_owned_storage_session(domain, directory / "absent",
											  std::shared_ptr<Admission>(new Admission{observed}), false);
			});
			auto drain = domain.close();
			drain.wait();
			check(observed.retired && !observed.wrong_thread);
			rejects(
				[&] { l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), false); });
		}
		eventually([&] { return account->used() == 0; });
		{
			auto domain = runtime.domain(account, 1);
			rejects([&] { l::make_owned_storage_session(domain, directory, {}, true); });
			check(domain.residents() == 0);
			auto empty_directory = directory / "empty";
			std::filesystem::create_directory(empty_directory);
			Observation observed{runtime};
			rejects([&] {
				l::make_owned_storage_session(domain, empty_directory,
											  std::shared_ptr<Admission>(new Admission{observed}), false);
			});
			auto drain = domain.close();
			drain.wait();
			check(observed.retired && !observed.wrong_thread && std::filesystem::is_empty(empty_directory));
		}
		eventually([&] { return account->used() == 0; });
		{
			auto domain = runtime.domain(account, 2);
			auto first = l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), false);
			rejects(
				[&] { l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), false); });
			eventually([&] { return domain.residents() == 1; });
			auto drain = domain.close();
			first.reset();
			drain.wait();
		}
		eventually([&] { return account->used() == 0; });
		{
			Observation observed{runtime};
			std::shared_ptr<c::IO> alias;
			{
				auto domain = runtime.domain(account, 1);
				auto session = l::make_owned_storage_session(
					domain, directory, std::shared_ptr<Admission>(new Admission{observed}), false);
				alias = session->backend();
				observed.directory = dynamic_cast<c::PosixIO *>(alias.get())->native_directory_handle();
			}
			// The domain facade has disappeared, but its admitted resident remains.
			check(!observed.retired);
			std::weak_ptr<c::IO> weak = alias;
			std::atomic<bool> entered{false}, stop{false};
			std::jthread reader([weak, &entered, &stop] {
				entered.store(true, std::memory_order_release);
				while (!stop.load(std::memory_order_acquire)) {
					auto held = weak.lock();
					if (!held)
						break;
					std::this_thread::yield();
				}
			});
			struct StopReader {
				std::atomic<bool> &stop;
				~StopReader() { stop.store(true, std::memory_order_release); }
			} stop_reader{stop};
			eventually([&] { return entered.load(std::memory_order_acquire); });
			alias.reset();
			eventually([&] { return observed.retired.load(std::memory_order_acquire); });
			stop.store(true, std::memory_order_release);
			reader.join();
			check(weak.expired() && !observed.wrong_thread && !observed.open_directory);
			weak.reset();
		}
		eventually([&] { return account->used() == 0; });
		{
			auto domain = runtime.domain(account, 1);
			Observation observed{runtime};
			auto session = l::make_owned_storage_session(
				domain, directory, std::shared_ptr<Admission>(new Admission{observed}), false);
			observed.directory =
				dynamic_cast<c::PosixIO *>(session->backend().get())->native_directory_handle();
			auto queue = c::make_native_queue(session->allocation_context());
			auto bytes = std::make_shared<const std::string>("retained native preparation");
			auto preparation = c::make_image_preparation(session->allocation_context(), session->backend(),
														 session, session->next_operation(), "held.image",
														 bytes, 64, c::PreparationFilePolicy::CloseAfterSeal);
			check(queue->submit(preparation));
			session->close();
			rejects([&] { session->next_operation(); });
			auto drain = domain.close();
			preparation.reset();
			session.reset();
			auto poll = [&] {
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				for (;;) {
					auto completion = queue->poll();
					if (completion)
						return completion;
					check(std::chrono::steady_clock::now() < deadline);
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			};
			auto held = poll();
			check(!held->completion.error && held->completion.file && !observed.retired && !drain.done());
			auto pressure = account->admit(account->limit() - account->used());
			for (;;) {
				preparation = std::static_pointer_cast<c::ImagePreparation>(held->operation);
				check(preparation->complete(std::move(held->completion)));
				check(!observed.retired && !drain.done());
				if (preparation->done())
					break;
				check(queue->submit(preparation));
				preparation.reset();
				held.reset();
				held = poll();
			}
			check(!preparation->error());
			preparation.reset();
			held.reset();
			drain.wait();
			check(observed.retired && !observed.wrong_thread && !observed.open_directory);
			// Queue destruction joins its fallback worker on this shutdown caller.
			queue.reset();
			pressure.reset();
		}
		eventually([&] { return account->used() == 0; });
		std::size_t failures = 0, successes = 0;
		for (unsigned mode : {0640u, 0644u}) {
			const auto shared_directory = directory / (mode == 0640 ? "group" : "public");
			std::filesystem::create_directory(shared_directory);
			std::filesystem::permissions(shared_directory, std::filesystem::perms::owner_all);
			{
				auto domain = runtime.domain(account, 1);
				auto session =
					l::make_owned_storage_session(domain, shared_directory, std::make_shared<int>(1), true, mode);
				auto bytes = std::make_shared<const std::string>("shared artifact");
				const auto name = mode == 0640 ? "group.image" : "public.image";
				auto preparation = c::make_image_preparation(
					session->allocation_context(), session->backend(), session, session->next_operation(),
					name, bytes, 64, session->backend()->make_closed_file());
				drive(*preparation);
				check(!preparation->error());
				check(session->backend()->inspect_entry(name)->permissions == mode);
				check(session->backend()->inspect_entry("owner.lock")->permissions == 0600);
				auto drain = domain.close();
				preparation.reset();
				session.reset();
				drain.wait();
			}
			eventually([&] { return account->used() == 0; });
		}
		{
			auto domain = runtime.domain(account, 1);
			rejects([&] {
				l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), false, 0666);
			});
			auto drain = domain.close();
			drain.wait();
		}
		eventually([&] { return account->used() == 0; });
		for (std::size_t available = 0; available <= bootstrap_capacity; ++available) {
			{
				auto domain = runtime.domain(account, 1);
				const auto domain_capacity = account->used();
				auto pressure = account->admit(account->limit() - domain_capacity - available);
				std::shared_ptr<l::StorageSession> session;
				try {
					session =
						l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), false);
					++successes;
				} catch (const std::bad_alloc &) {
					++failures;
				} catch (const d::StoragePressure &) {
					++failures;
				}
				auto drain = domain.close();
				session.reset();
				drain.wait();
				check(domain.residents() == 0);
				eventually([&] { return account->used() == domain_capacity + pressure.bytes(); });
			}
			eventually([&] { return account->used() == 0; });
		}
		check(failures == bootstrap_capacity && successes == 1);
		std::cout << "bootstrap admission sweep: " << failures << " failures, " << successes << " success\n";
		runtime.shutdown();
		std::filesystem::remove_all(directory);
		std::cout << "session native retirement and alias qualification passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
