#!/usr/bin/env python3
"""Adds per-phase wall-time accounting to a copy of liveness_test.cc: instrument.py IN OUT."""
import re, sys
s = open(sys.argv[1]).read()
hdr = '''
#include <chrono>
#include <mutex>
namespace {
struct PhaseClock { std::map<std::string, double> sec; std::map<std::string, std::uint64_t> events; };
PhaseClock phases; std::mutex phases_lock; thread_local std::string current_profile; thread_local std::uint64_t event_count = 0;
struct PhaseTimer { std::string name; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now(); std::uint64_t e0 = event_count;
  explicit PhaseTimer(std::string n) : name(std::move(n)) {}
  ~PhaseTimer() { std::lock_guard g(phases_lock); phases.sec[current_profile + " | " + name] += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); phases.events[current_profile + " | " + name] += event_count - e0; } };
}
'''
s = s.replace('using namespace kronuz::consensus;', 'using namespace kronuz::consensus;\n' + hdr, 1)
# count every core step (main's harness and the held-Tick one name the call differently)
for call in ('auto actions = nodes_.at(id).core->step(std::move(event));', 'auto actions = n.core->step(std::move(event));'):
    if s.count(call) == 1: s = s.replace(call, call + ' ++event_count;', 1); break
else: sys.exit('no core step call to count')
marks = [
 ('// A healed partition elects and commits.\n\t\t{', 'heal'),
 ('// A partitioned follower never raises the healthy group\'s term.\n\t\t{', 'lone'),
 ('// A transfer to a reachable voter completes.\n\t\t{', 'transfer'),
 ('// off to a data voter within election_max.\n\t\t{', 'witness'),
 ('// the promotion commits, before the leaving voter\'s removal does).\n\t\tfor (bool replace : {false, true}) {', 'promotion+replacement'),
 ('// the remaining group\'s term once it returns.\n\t\t{', 'removed'),
 ('// A leader that cannot receive steps down, and a new leader commits.\n\t\t{', 'deaf'),
]
for m, name in marks:
    assert s.count(m) == 1, m
    s = s.replace(m, m + f'\n\t\t\tPhaseTimer _t("{name}");')
s = s.replace('void run(const Profile& profile, std::uint64_t seeds) {', 'void run(const Profile& profile, std::uint64_t seeds) {\n\tcurrent_profile = profile.name;\n\tconst auto run_t0 = std::chrono::steady_clock::now();', 1)
s = s.replace('\tconst auto heal99 = percentile(heal, 0.99)', '\t{ std::lock_guard g(phases_lock); phases.sec[current_profile + " | TOTAL seeds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - run_t0).count(); }\n\tconst auto heal99 = percentile(heal, 0.99)', 1)
s = s.replace('\tstd::printf("%d failures\\n", failures);', '\tfor (auto& [k, v] : phases.sec) { std::printf("TIME %-52s %8.2f s %12llu events\\n", k.c_str(), v, (unsigned long long)phases.events[k]); }\n\tstd::printf("%d failures\\n", failures);', 1)
open(sys.argv[2], 'w').write(s)
