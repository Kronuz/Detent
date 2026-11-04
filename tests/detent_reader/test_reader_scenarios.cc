// The C++ runner of bindings/tests/scenarios.json, the table the Python and Rust tests also run
// (docs/design/client-local-source.md D4). Usage: test_reader_scenarios scenarios.json
#include "detent/reader.hh"
#include "detent_testing/generation_writer.hh"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {
[[noreturn]] void fail(const std::string &what) { throw std::runtime_error(what); }

std::optional<std::string_view> text(const rapidjson::Value &object, const char *key) {
	if (!object.HasMember(key) || object[key].IsNull())
		return std::nullopt;
	const auto &value = object[key];
	return std::string_view(value.GetString(), value.GetStringLength());
}

std::string dump(const rapidjson::Value &value) {
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	value.Accept(writer);
	return buffer.GetString();
}

// Compares a looked-up or expected value with the table's `value` (bool, integer or null).
void expect_value(const rapidjson::Value &want, bool present, const detent_value &got, const std::string &where) {
	if (want.IsNull()) {
		if (present)
			fail(where + ": expected no value");
		return;
	}
	if (!present)
		fail(where + ": expected a value");
	if (want.IsBool()) {
		if (got.kind != DETENT_KIND_BOOL || got.boolean != want.GetBool())
			fail(where + ": bool differs");
	} else if (got.kind != DETENT_KIND_INT || got.integer != want.GetInt64()) {
		fail(where + ": integer differs");
	}
}

void run(const rapidjson::Value &c, const detent::Reader &reader) {
	const std::string op = c["op"].GetString();
	const auto &want = c["value"];
	const std::string where = op + " " + dump(c);
	if (op == "revision") {
		const auto got = reader.revision(c["namespace"].GetString());
		if (want.IsNull() != !got.has_value() || (got && *got != want.GetUint64()))
			fail(where + ": revision differs");
		return;
	}
	const std::string_view name = c["name"].GetString();
	const auto hash = text(c, "hash");
	const auto switch_value = text(c, "switch");
	if (op == "eval") {
		if (reader.eval(name, c["fallback"].GetBool(), hash, switch_value) != want.GetBool())
			fail(where + ": bool differs");
	} else if (op == "get") {
		if (reader.get(name, c["fallback"].GetInt64()) != want.GetInt64())
			fail(where + ": integer differs");
	} else if (op == "lookup") {
		const auto got = reader.lookup(name, hash, switch_value);
		if (std::string(detent_status_name(got.status)) != c["status"].GetString())
			fail(where + ": status " + detent_status_name(got.status));
		expect_value(want, got.status == DETENT_OK, got.value, where);
	} else {
		fail("unknown op " + op);
	}
}
} // namespace

int main(int argc, char **argv) {
	if (argc != 2)
		fail("usage: test_reader_scenarios scenarios.json");
	std::ifstream in(argv[1]);
	std::stringstream contents;
	contents << in.rdbuf();
	rapidjson::Document doc;
	doc.Parse(contents.str().c_str());
	if (doc.HasParseError())
		fail("scenarios.json does not parse");

	const auto root = std::filesystem::current_path() / ".scratch" / ("reader-scenarios-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};

	std::vector<std::string> names;
	for (const auto &ns : doc["namespaces"].GetArray())
		names.emplace_back(ns.GetString());
	const std::vector<std::string_view> namespaces(names.begin(), names.end());

	detent::testing::GenerationWriter writer(root / "store");
	std::size_t checked = 0;
	std::optional<detent::Reader> reader;
	for (rapidjson::SizeType publish = 0; publish < doc["publishes"].Size(); ++publish) {
		writer.publish_json(dump(doc["publishes"][publish]));
		if (!reader)
			reader.emplace((root / "store").string(), ::getuid(), namespaces);
		for (const auto &c : doc["cases"].GetArray()) {
			if (c["after"].GetUint() != publish + 1)
				continue;
			run(c, *reader);
			++checked;
		}
	}
	if (checked != doc["cases"].Size())
		fail("a case names a publish that does not exist");
	std::cout << "ok: " << checked << " scenarios\n";
	return 0;
}
