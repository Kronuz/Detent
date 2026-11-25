// The C++ runner of bindings/tests/scenarios.json, the table the Python and Rust tests also run
// (docs/design/client-local-source.md D4). Usage: test_reader_scenarios scenarios.json [DETENT]: with the
// `detent` binary, its `get` prints each integer row's value over the same store (D148).
#include "detent/reader.hh"
#include "detent_testing/generation_writer.hh"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

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
// `detent get` of a `get` or `lookup` row, run as a user read of the store with the row's fallback as
// --default: it prints the row's value, or the fallback for any status but ok.
bool run_cli(const rapidjson::Value &c, const std::string &detent, const std::filesystem::path &root,
             const std::vector<std::string> &opened) {
	const std::string op = c["op"].GetString();
	if (op != "get" && op != "lookup")
		return false;
	const std::string name = c["name"].GetString();
	// `detent get` opens the name's own namespace, so a row outside the table's list has no CLI twin.
	const auto colon = name.find(':');
	if (colon != name.npos && std::find(opened.begin(), opened.end(), name.substr(0, colon)) == opened.end())
		return false;
	const auto fallback = std::to_string(c["fallback"].GetInt64());
	const bool ok = op == "get" || std::string_view(c["status"].GetString()) == "ok";
	const auto expected = (ok ? std::to_string(c["value"].GetInt64()) : fallback) + "\n";
	std::vector<std::string> args{detent, "get", name, "--default", fallback, "--config", (root / "agent.json").string(),
	                              "--generation-dir", (root / "store").string(), "--publisher-user",
	                              std::to_string(::getuid())};
	for (const char *flag : {"hash", "switch"})
		if (const auto value = text(c, flag)) {
			args.push_back(std::string("--") + flag);
			args.emplace_back(*value);
		}
	int out[2];
	if (::pipe(out) != 0)
		fail("pipe");
	const auto child = ::fork();
	if (child == 0) {
		::dup2(out[1], 1);
		::close(out[0]);
		::close(out[1]);
		std::vector<char *> argv;
		for (auto &arg : args)
			argv.push_back(arg.data());
		argv.push_back(nullptr);
		::execv(argv[0], argv.data());
		::_exit(127);
	}
	::close(out[1]);
	std::string printed;
	char buffer[256];
	for (ssize_t n; (n = ::read(out[0], buffer, sizeof buffer)) > 0;)
		printed.append(buffer, std::size_t(n));
	::close(out[0]);
	int status = 0;
	::waitpid(child, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || printed != expected)
		fail("detent get " + dump(c) + ": exit " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) +
		     ", printed '" + printed + "'");
	return true;
}
} // namespace

int main(int argc, char **argv) {
	if (argc != 2 && argc != 3)
		fail("usage: test_reader_scenarios scenarios.json [DETENT]");
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

	// The agent's layout: an entry per namespace (namespace-delivery D8).
	detent::testing::GenerationWriter writer(root / "store", detent::testing::GenerationWriter::Layout::PerNamespace);
	std::ofstream(root / "agent.json") << "{}\n";
	std::size_t checked = 0, cli = 0;
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
			if (argc == 3 && run_cli(c, argv[2], root, names))
				++cli;
		}
	}
	if (checked != doc["cases"].Size())
		fail("a case names a publish that does not exist");
	std::cout << "ok: " << checked << " scenarios" << (argc == 3 ? ", detent get on " + std::to_string(cli) : "")
	          << "\n";
	return 0;
}
