#include "detent_local/reader.hh"
#include "detent_testing/generation_writer.h"
#include "detent_testing/generation_writer.hh"
#include "prepared_reader.hh"
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace l = detent::local;
namespace x = l::detail;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("generation writer invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)
std::string knobs(std::int64_t rev, bool flag) {
	return R"({"schema_version":1,"knobs":{"t:rev":{"default":)" + std::to_string(rev) +
	       R"(},"t:flag":{"default":)" + (flag ? "1" : "0") +
	       R"(,"overrides":{"vip":1}},"u:ramp":{"default":0,"rollout_pct":100,"rollout_value":1}}})";
}
} // namespace
int main() {
	const auto root =
	    std::filesystem::current_path() / ".scratch" / ("generation-writer-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	{
		detent::testing::GenerationWriter writer(root / "cpp");
		CHECK(writer.revision() == 0);
		CHECK(writer.publish_json(knobs(1, false)) == 1);
		auto seen = x::PreparedReader::open(writer.store(), ::getuid(), {});
		CHECK(seen.view.get("t", "rev").value() == 1);
		CHECK(seen.view.get("t", "flag").value() == 0);
		CHECK(seen.view.get("t", "flag", std::nullopt, std::string_view("vip")).value() == 1);
		CHECK(seen.view.get("u", "ramp", std::string_view("anyone")).value() == 1);
		CHECK(!seen.view.get("t", "absent").has_value());
		// Each publication is a new real generation; an attached view keeps its own.
		for (std::int64_t rev = 2; rev != 7; ++rev) {
			CHECK(writer.publish_json(knobs(rev, rev % 2 == 0)) == std::uint64_t(rev));
			auto next = x::PreparedReader::open(writer.store(), ::getuid(), {});
			CHECK(next.view.get("t", "rev").value() == rev);
			CHECK(next.view.get("t", "flag").value() == (rev % 2 == 0 ? 1 : 0));
			CHECK(next.descriptor.cursor.revision == std::uint64_t(rev));
			CHECK(seen.view.get("t", "rev").value() == 1);
		}
	}
	{
		// Reopening an existing store continues its revisions.
		detent::testing::GenerationWriter again(root / "cpp");
		CHECK(again.revision() == 6);
		CHECK(again.publish_json(knobs(70, true)) == 7);
		auto view = x::PreparedReader::open(again.store(), ::getuid(), {});
		CHECK(view.view.get("t", "rev").value() == 70);
	}
	{
		const auto path = (root / "c").string();
		auto *writer = detent_testing_open(path.c_str());
		CHECK(writer != nullptr);
		const auto json = knobs(9, true);
		std::uint64_t revision = 0;
		CHECK(detent_testing_publish(writer, json.data(), json.size(), &revision) == 0 && revision == 1);
		const auto bad = std::string("{not json");
		CHECK(detent_testing_publish(writer, bad.data(), bad.size(), &revision) == -1);
		CHECK(std::string(detent_testing_error(writer)).size() > 0 && revision == 1);
		auto view = x::PreparedReader::open(path, ::getuid(), {});
		CHECK(view.view.get("t", "rev").value() == 9);
		detent_testing_close(writer);
	}
	std::cout << "ok\n";
	return 0;
}
