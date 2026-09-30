// Section 11: CLI override parsing and precedence over both baked defaults
// and (Section 13) a later live snapshot.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <random>

#include "detent/client.hh"
#include "detent/overrides.hh"

using namespace detent;

namespace {

std::filesystem::path write_temp_file(std::string_view contents) {
	static std::mt19937_64 rng{std::random_device{}()};
	auto path = std::filesystem::temp_directory_path() /
	            ("detent_test_" + std::to_string(rng()) + ".json");
	std::ofstream out(path, std::ios::binary);
	out << contents;
	out.close();
	return path;
}

void test_cli_override_parsing() {
	auto ok = parse_cli_overrides({"checkout:killswitch=true", "search:max_results=100"});
	assert(ok.has_value());
	assert(ok.value().values.size() == 2);

	// Duplicate names are a startup error -- no last-one-wins.
	auto dup = parse_cli_overrides({"checkout:killswitch=true", "checkout:killswitch=false"});
	assert(!dup.has_value());

	// Malformed value (not true/false, not a valid integer).
	auto bad_int = parse_cli_overrides({"search:max_results=not-a-number"});
	assert(!bad_int.has_value());

	// Malformed pair (no '=').
	auto bad_pair = parse_cli_overrides({"checkout:killswitch"});
	assert(!bad_pair.has_value());

	std::puts("CLI override parsing OK");
}

void test_cli_override_precedence() {
	auto defaults_path = write_temp_file(R"({
		"schema_version": 1,
		"knobs": {
			"checkout:killswitch_new_path": { "type": "bool", "default": false },
			"search:max_results": { "type": "int", "default": 25 }
		}
	})");

	InitOptions opts;
	opts.defaults_path = defaults_path;
	opts.cli_overrides = {"checkout:killswitch_new_path=true", "search:max_results=999"};

	Client c(std::move(opts));

	// Override shadows the baked default.
	auto b = c.eval("checkout:killswitch_new_path");
	assert(b.has_value() && b.value() == true);
	auto i = c.get("search:max_results");
	assert(i.has_value() && i.value() == 999);

	// Wrong-typed override read is an error, not a silent coercion.
	auto wrong = c.get("checkout:killswitch_new_path");
	assert(!wrong.has_value());
	assert(wrong.error().category == ErrorCategory::WrongType);

	auto h = c.health();
	assert(h.cli_overrides == 2);

	std::filesystem::remove(defaults_path);
	std::puts("CLI override precedence over baked defaults OK");
}

} // namespace

int main() {
	test_cli_override_parsing();
	test_cli_override_precedence();
	return 0;
}
