#include "detentd.hh"
#include <raft.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace c = cluster;
void check(bool value) {
	if (!value) throw std::runtime_error("Raft application retry invariant");
}
struct Delegate final : c::RaftDelegate<std::string> {
	detentd::Distributor& app;
	unsigned attempts = 0, responses = 0;
	explicit Delegate(detentd::Distributor& owner) : app(owner) {}
	void broadcast(c::RaftMessage, const std::string&) override { ++responses; }
	std::string local_node() override { return "A"; }
	std::string serialise(const std::string& node) override { return c::serialise_string(node); }
	std::optional<std::string> parse_node(const char** p, const char* end) override {
		std::string_view node;
		if (!c::unserialise_string(p, end, node) || (node != "A" && node != "B" && node != "C")) return {};
		return std::string(node);
	}
	std::string node_id(const std::string& node) override { return node; }
	std::size_t total_nodes() override { return 3; }
	std::size_t alive_nodes() override { return 3; }
	bool quorum(std::size_t total, std::size_t count) override { return count > total / 2; }
	bool prefers(const std::string& a, const std::string& b) override { return a == b; }
	bool is_alive(const std::string&) override { return true; }
	bool active() override { return true; }
	bool ready() override { return true; }
	bool joining() override { return false; }
	void ensure_setup() override {}
	void set_leader(const std::string&) override {}
	void apply(const std::string& command) override { ++attempts; app.on_raft_apply(command); }
};
struct Directory {
	std::filesystem::path path;
	explicit Directory(const char* parent) : path(std::filesystem::path(parent) / ("retry-" + std::to_string(::getpid()))) {
		check(std::filesystem::create_directory(path));
	}
	~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
int main(int argc, char** argv) {
	try {
		check(argc == 2);
		Directory directory(argv[1]);
		detentd::Options options;
		options.persist_path = directory.path / "missing" / "snapshot.json";
		{
			asio::io_context io;
			detentd::Distributor app(options);
			Delegate delegate(app);
			c::Raft<std::string> raft(io.get_executor(), {}, &delegate);
			const auto header = c::serialise_string("B") + c::serialise_length(1);
			const auto command = c::serialise_string("retry") + 'P' + R"({"schema_version":1,"version":1,"knobs":{}})";
			const auto data = header + c::serialise_length(0) + c::serialise_length(0) + c::serialise_length(1) +
				c::serialise_length(1) + c::serialise_length(1) + c::serialise_string(command);
			bool failed = false;
			try { raft.on_message(c::RaftMessage::APPEND_ENTRIES, data); }
			catch (const std::exception&) { failed = true; }
			check(failed && app.current_version() == 0 && delegate.responses == 0);
			check(!std::filesystem::exists(options.persist_path));
			std::filesystem::create_directory(directory.path / "missing");
			const auto heartbeat = header + c::serialise_length(1) + c::serialise_length(1) + c::serialise_length(1);
			raft.on_message(c::RaftMessage::HEARTBEAT, heartbeat);
			check(app.current_version() == 1 && app.puts_accepted() == 1 && delegate.attempts == 2 && delegate.responses == 1);
			raft.on_message(c::RaftMessage::HEARTBEAT, heartbeat);
			check(app.puts_accepted() == 1 && delegate.attempts == 2 && delegate.responses == 2);
			raft.stop(); io.poll();
		}
		detentd::Distributor recovered(options);
		check(recovered.current_version() == 1);
		std::cout << "unchanged Raft commit recovers after genuine persistence failure\n";
		return 0;
	} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
