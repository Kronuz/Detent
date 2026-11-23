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
	if (op == "get") {
		if (reader.get(name, c["fallback"].GetInt64(), hash, switch_value) != want.GetInt64())
			fail(where + ": integer differs");
	} else if (op == "get_as") {
		const std::string type = c["type"].GetString();
		const auto &fallback = c["fallback"];
		bool same = false;
		if (type == "bool")
			same = reader.get_as<bool>(name, fallback.GetBool(), hash, switch_value) == want.GetBool();
		else if (type == "int8")
			same = reader.get_as<std::int8_t>(name, std::int8_t(fallback.GetInt()), hash, switch_value) == want.GetInt();
		else if (type == "uint8")
			same = reader.get_as<std::uint8_t>(name, std::uint8_t(fallback.GetUint()), hash, switch_value) == want.GetUint();
		else if (type == "int64")
			same = reader.get_as<std::int64_t>(name, fallback.GetInt64(), hash, switch_value) == want.GetInt64();
		else if (type == "uint64")
			same = reader.get_as<std::uint64_t>(name, fallback.GetUint64(), hash, switch_value) == want.GetUint64();
		else
			fail(where + ": unknown type " + type);
		if (!same)
			fail(where + ": " + type + " differs");
	} else if (op == "lookup") {
		const auto got = reader.lookup(name, c["fallback"].GetInt64(), hash, switch_value);
		if (std::string(detent_status_name(got.status)) != c["status"].GetString())
			fail(where + ": status " + detent_status_name(got.status));
		if (got.value != want.GetInt64())
			fail(where + ": value differs");
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
