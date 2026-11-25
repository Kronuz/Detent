#!/usr/bin/env python3
"""Adds a trajectory hash to a copy of liveness_test.cc: trace.py IN OUT. Every non-empty action
list a core returns mixes (time, node, each action's kind, a Send's message kind and term) into
a hash printed per profile as TRACE; two harnesses with equal TRACE lines drove the cores
through the same actions at the same times."""
import sys
s = open(sys.argv[1]).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b, 1)
rep('using namespace kronuz::consensus;', '''using namespace kronuz::consensus;
namespace { thread_local std::uint64_t trace_hash = 1469598103934665603ull, trace_actions = 0;
void mix(std::uint64_t v) { trace_hash = (trace_hash ^ v) * 1099511628211ull; } }''')
rep('''	void consume(NodeId id, Actions& actions) {
''', '''	void consume(NodeId id, Actions& actions) {
		if (!actions.empty()) { mix(now_); mix(id); }
		for (auto& a : actions) {
			++trace_actions; mix(a.index());
			if (auto send = std::get_if<Send>(&a)) { mix(send->peer); mix(send->message.index()); mix(std::visit([](const auto& m) { return std::uint64_t(m.term); }, send->message)); }
		}
''')
out = 'say' if 'void say(' in s else 'std::printf'
rep('''	const auto heal99 = percentile(heal, 0.99)''', '''	''' + out + '''("TRACE %s %016llx %llu actions\\n", profile.name, (unsigned long long)trace_hash, (unsigned long long)trace_actions);
	trace_hash = 1469598103934665603ull; trace_actions = 0;
	const auto heal99 = percentile(heal, 0.99)''')
open(sys.argv[2], 'w').write(s)
