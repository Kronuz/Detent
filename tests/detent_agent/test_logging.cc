#include "../../src/detent_cli/logging.hh"
#include <cassert>
#include <stdexcept>

int main() {
	using namespace detent_logging;
	std::vector<std::string> args{"start", "-v", "--color=never", "--runtime-dir", "run"};
	auto settings = parse(args, {});
	assert(settings.level == LOG_DEBUG);
	assert(settings.color == LogColorMode::never);
	assert((args == std::vector<std::string>{"start", "--runtime-dir", "run"}));
	args = {"-vv", "--no-color", "--color", "always"};
	settings = parse(args, {{"--log-level", "warning"}, {"--verbosity", "1"}});
	assert(settings.level == LOG_DEBUG);
	assert(settings.color == LogColorMode::always);
	assert(args.empty());
	args = {};
	assert(parse(args, {{"--verbosity", "0"}}).level == LOG_INFO);
	for (const auto& [name, depth] : std::vector<std::pair<std::string, LogColorDepth>>{
		{"truecolor", LogColorDepth::truecolor}, {"24bit", LogColorDepth::truecolor},
		{"256", LogColorDepth::ansi256}, {"256color", LogColorDepth::ansi256},
		{"16", LogColorDepth::ansi16}, {"ansi", LogColorDepth::ansi16},
		{"stacked", LogColorDepth::stacked}}) {
		args = {"--color", name};
		settings = parse(args, {});
		assert(settings.color == LogColorMode::always);
		assert(settings.color_depth == depth);
		assert(args.empty());
	}
	for (const auto& bad : std::vector<std::vector<std::string>>{{"--color"}, {"--color=bad"}, {"--log-level", "bad"}, {"--verbosity", "3"}, {"--verbosity=-1"}, {"--verbosity=1x"}}) {
		args = bad;
		bool rejected = false;
		try { (void)parse(args, {}); } catch (const std::invalid_argument&) { rejected = true; }
		assert(rejected);
	}
}
