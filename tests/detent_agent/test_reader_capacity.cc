#include "../../src/detent_agent/config.hh"
#include "../../src/detent_agent/reader_capacity.hh"
#include <cassert>
#include <fstream>
#include <sys/stat.h>

using detent_agent::ReaderCapacity;
static_assert(!std::is_move_constructible_v<ReaderCapacity>);
static_assert(!std::is_copy_constructible_v<ReaderCapacity>);
int main(int argc, char **argv) {
	assert(argc == 2);
	const std::filesystem::path root(argv[1]), path = root / "agent.json";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	auto write = [&](std::string_view value) {
		std::ofstream file(path); file << value; file.close();
		assert(::chmod(path.c_str(), 0644) == 0);
	};
	auto load = [&] { return detent_agent::load_config({"--config", path.string()}); };
	auto refused = [&](auto &&call) {
		bool rejected = false;
		try { call(); } catch (const std::exception &) { rejected = true; }
		assert(rejected);
	};
	ReaderCapacity defaults([](const auto &, const char *) { return std::optional<std::string>{}; });
	assert(defaults.limits().records == 500000 && !defaults.limits().retirement_domain);
	write(R"({"reader_records":5000000,"reader_heap_bytes":3221225472,"reader_mapping_bytes":3221225472})");
	const auto config = load();
	assert(config.at("--reader-heap-bytes") == "3221225472");
	std::shared_ptr<detent::distribution::StorageLedger> heap, mapping;
	{
		ReaderCapacity capacity([&](const auto &flag, const char *) -> std::optional<std::string> {
			const auto found = config.find(flag);
			return found == config.end() ? std::nullopt : std::optional(found->second);
		});
		assert(capacity.limits().records == 5000000);
		heap = capacity.limits().retirement_domain->storage_account();
		mapping = capacity.limits().mapping_account;
	}
	assert(heap->used() == 0 && mapping->used() == 0);
	assert(::chmod(path.c_str(), 0664) == 0);
	refused(load);
	write(R"({"reader_records":1,"reader_records":2})"); refused(load);
	write(R"({"reader_heap_bytes":18446744073709551616})"); refused(load);
	write(R"({"reader_overrides":0})"); assert(load().at("--reader-overrides") == "0");
	const auto link = root / "link.json";
	std::filesystem::create_symlink(path.filename(), link);
	refused([&] { detent_agent::load_config({"--config", link.string()}); });
	write(R"({"poll_interval_ms":10})");
	assert(detent_agent::load_config({"--config", link.string()}).at("--poll-interval-ms") == "10");
	for (const auto value : {"4294967296", "-1", "18446744073709551616"})
		refused([&] {
			ReaderCapacity capacity([&](const auto &flag, const char *) -> std::optional<std::string> {
				return flag == "--reader-records" ? std::optional<std::string>(value) : std::nullopt;
			});
		});
	std::filesystem::remove_all(root);
}
