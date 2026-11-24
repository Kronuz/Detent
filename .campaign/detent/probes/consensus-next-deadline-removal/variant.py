#!/usr/bin/env python3
"""variant.py IN OUT MODE: a copy of liveness_test.cc, already hashed by trace.py, in which every
node's Tick is stepped (MODE stepped) or every Tick before Core::next_deadline() is held whatever
the node's role and membership (MODE held): a leader with a member leaving or an unsettled
membership (the D3 removal), a witness leader (D9), a non-voter. LIVENESS_VERIFY also fails a held
Tick that retires log entries. Each profile prints HELD: the held Ticks in each of those classes."""
import re
import sys

src, out, mode = sys.argv[1:]
assert mode in ("stepped", "held"), mode
s = open(src).read()


def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b, 1)


m = re.search(r"\tbool holdable\(NodeId id\) \{\n.*?\n\t\}\n", s, re.S)
assert m, "no holdable()"
s = s[: m.start()] + """\tbool holdable(NodeId) { return %s; }
\tvoid count_held(NodeId id) {
\t\tconst Core& core = *nodes_.at(id).core;
\t\tconst auto& m = core.membership();
\t\tif (!votes_in(m, id)) { ++held_nonvoter; return; }
\t\tif (core.role() != Role::Leader) { return; }
\t\tif (find_member(m, id)->role == MemberRole::Witness) { ++held_witness; }
\t\tif (m.leaving || core.committed_membership() != m) { ++held_leaving; }
\t}
""" % ("true" if mode == "held" else "false") + s[m.end():]
rep("using namespace kronuz::consensus;", """using namespace kronuz::consensus;
namespace { thread_local unsigned long long held_leaving = 0, held_witness = 0, held_nonvoter = 0; }""")
rep("\t\tif (tick.now < *n.hold_until) {\n", "\t\tif (tick.now < *n.hold_until) {\n\t\t\tcount_held(id);\n")
rep("""\t\t\tauto actions = step(id, tick);
\t\t\tcheck(actions.empty() && n.core->next_deadline() == deadline, "a held Tick changes nothing");""",
    """\t\t\tconst auto retired = n.core->retired_log_entries();
\t\t\tauto actions = step(id, tick);
\t\t\tcheck(actions.empty() && n.core->next_deadline() == deadline && n.core->retired_log_entries() == retired, "a held Tick changes nothing");""")
rep("\ttrace_hash = 1469598103934665603ull; trace_actions = 0;\n\tconst auto heal99",
    """\tsay("HELD leaving %llu witness %llu nonvoter %llu: %s\\n", held_leaving, held_witness, held_nonvoter, profile.name);
\theld_leaving = held_witness = held_nonvoter = 0;
\ttrace_hash = 1469598103934665603ull; trace_actions = 0;
\tconst auto heal99""")
open(out, "w").write(s)
