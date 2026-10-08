#include "pinned_source.hh"
#include "snapshot_fixture.hh"
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace l = detent::local;
namespace {
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream stream(path, std::ios::binary);
	stream.write(bytes.data(), bytes.size());
	stream.close();
	require(bool(stream));
	std::filesystem::permissions(path,
								 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
void put_cursor(std::string &out, d::Cursor c) {
	m::array(out, 5);
	for (auto *id : {&c.cluster, &c.group, &c.restore_epoch})
		m::string(out, {id->data(), id->size()}, true);
	m::string(out, {c.scope.data(), c.scope.size()}, true);
	m::unsigned_integer(out, c.revision);
}
std::shared_ptr<const d::ValidatedDelta> delta(bool progress, unsigned transactions = 2) {
	std::string out;
	auto a = [&](unsigned n) { m::array(out, n); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(out, v); };
	auto s = [&](std::string_view v) { m::string(out, v); };
	a(progress ? 5 : 7);
	n(1);
	n(progress ? 3 : 2);
	a(2);
	n(1);
	n(1);
	put_cursor(out, cursor());
	auto target = cursor();
	target.revision = 43 + transactions;
	put_cursor(out, target);
	if (!progress) {
		a(4);
		n(transactions);
		n(transactions * 2);
		n(0);
		n(transactions * 20);
		a(transactions);
		for (unsigned tx = 0; tx < transactions; ++tx) {
			a(3);
			n(43 + tx);
			a(1);
			a(2);
			s("app");
			n(43 + tx);
			a(2);
			a(4);
			n(0);
			s("app:count");
			n(tx == 0 ? 1 : 0);
			if (!tx)
				out.push_back('\xc0');
			else {
				a(2);
				n(1);
				m::signed_integer(out, tx);
			}
			a(4);
			n(0);
			s("app:flag");
			n(0);
			a(4);
			n(0);
			out.push_back(tx % 2 ? '\xc3' : '\xc2');
			out.push_back('\xc0');
			a(0);
		}
	}
	d::DeltaDecoder decoder(cursor(), {"app"}, {}, progress ? d::UpdateKind::Progress : d::UpdateKind::Delta);
	auto input = std::span<const char>(out.data(), out.size());
	while (!input.empty()) {
		d::WorkBudget budget{1, 1};
		decoder.feed(input, budget);
	}
	return std::make_shared<const d::ValidatedDelta>(decoder.finish());
}
// Independent snapshot oracle: spell the expected final tuple stream directly.
std::shared_ptr<const d::ValidatedSnapshot> expected(bool progress, unsigned tx) {
	auto target = cursor();
	target.revision = 43 + tx;
	if (progress)
		return std::make_shared<const d::ValidatedSnapshot>(
			decode(fixture(1, 24, 25, target.revision), target));
	std::string out;
	auto a = [&](unsigned v) { m::array(out, v); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(out, v); };
	auto text = [&](std::string_view v) { m::string(out, v); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(1);
	put_cursor(out, target);
	a(4);
	n(1);
	n(tx == 1 ? 1 : 2);
	n(0);
	n(tx == 1 ? 11 : 20);
	a(1);
	a(2);
	text("app");
	n(42 + tx);
	a(tx == 1 ? 1 : 2);
	if (tx > 1) {
		a(4);
		n(0);
		text("app:count");
		n(42 + tx);
		a(2);
		n(1);
		m::signed_integer(out, tx - 1);
	}
	a(4);
	n(0);
	text("app:flag");
	n(42 + tx);
	a(4);
	n(0);
	out.push_back((tx - 1) % 2 ? '\xc3' : '\xc2');
	out.push_back('\xc0');
	a(0);
	return std::make_shared<const d::ValidatedSnapshot>(decode(out, target));
}
void complete(l::ImageBuilder &builder) {
	std::size_t turns = 0;
	while (!builder.done()) {
		l::ValidationBudget budget{1, 1};
		builder.step(budget);
		require(++turns < 5000000);
	}
}
} // namespace
int main() {
	try {
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("applied-builder-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		l::GenerationId id;
		id.fill('a');
		l::ImageBuilder original(source, id);
		complete(original);
		const auto name = l::generation_name(id);
		write(directory / (name + ".image"), {original.bytes()->data(), original.bytes()->size()});
		write(directory / (name + ".lease"), {});
		write(directory / "selected", l::encode_descriptor(original.descriptor()));
		for (bool progress : {false, true})
			for (unsigned tx : {1u, 2u, 17u}) {
				auto input = delta(progress, tx);
				auto next = id;
				next.fill('b');
				l::ImageBuilder builder(input, l::recover_generation(directory), next);
				complete(builder);
				require(builder.base_descriptor() == original.descriptor());
				require(builder.base_directory_identity() ==
						l::recover_generation(directory).directory_identity());
				auto selected = builder.descriptor();
				l::ImageBuilder oracle(expected(progress, tx), next);
				complete(oracle);
				require(oracle.descriptor() == selected);
				require(std::equal(oracle.bytes()->data(), oracle.bytes()->data() + oracle.bytes()->size(),
								   builder.bytes()->data()));
				const auto next_name = l::generation_name(next);
				write(directory / (next_name + ".image"), {builder.bytes()->data(), builder.bytes()->size()});
				write(directory / (next_name + ".lease"), {});
				write(directory / "selected", l::encode_descriptor(selected));
				{
					l::detail::PinnedImageSource result(l::recover_generation(directory));
					require(result.descriptor().cursor == input->target());
					require(result.name_space(0).revision == (progress ? 42 : 42 + tx));
					require(result.record_count() == (!progress && tx == 1 ? 1 : 2));
					if (progress) {
						require(result.record(0).revision == 41 && result.record(1).revision == 42);
						require(
							std::get<l::detail::SourceBoolean>(result.record(1).definition).override_count ==
							1);
					} else {
						auto flag = result.record(result.record_count() - 1);
						require(flag.key == "app:flag" && flag.revision == 42 + tx);
						require(std::get<l::detail::SourceBoolean>(flag.definition).default_value ==
								bool((tx - 1) % 2));
						if (tx > 1)
							require(std::get<detent::IntDef>(result.record(0).definition).default_value ==
									tx - 1);
					}
				}
				bool rejected = false;
				try {
					l::ImageBuilder wrong(input, l::recover_generation(directory), id);
				} catch (const std::invalid_argument &) {
					rejected = true;
				}
				require(rejected);
				write(directory / "selected", l::encode_descriptor(original.descriptor()));
			}

		{
			auto input = delta(false);
			auto account = input->storage_account();
			const auto before = account->used();
			{
				l::ImageBuilder builder(input, l::recover_generation(directory), id);
				const auto controls = account->used();
				auto pressure = account->admit(account->limit() - controls);
				bool rejected = false;
				try {
					l::ValidationBudget budget;
					builder.step(budget);
				} catch (const d::StoragePressure &) {
					rejected = true;
				}
				require(rejected && account->used() == account->limit());
				pressure.reset();
				while (true) {
					l::ValidationBudget budget{1, 1};
					if (builder.discard(budget))
						break;
				}
				require(account->used() == controls && !builder.done());
			}
			require(account->used() == before);
		}
		{
			auto input = delta(false);
			l::ViewLimits limits;
			limits.records = 0;
			l::ImageBuilder builder(input, l::recover_generation(directory), id, limits);
			bool rejected = false;
			try {
				complete(builder);
			} catch (const std::length_error &) {
				rejected = true;
			}
			require(rejected);
			std::size_t turns = 0;
			while (true) {
				l::ValidationBudget budget{1, 1};
				if (builder.discard(budget))
					break;
				require(++turns < 5000000);
			}
		}
		std::filesystem::remove_all(directory);
		std::cout << "exact-base repeated updates, deletion and progress passed\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
