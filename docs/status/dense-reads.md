# Dense reads and validation

Implemented bounded dense-node reads, exact verified starts, one-digest first touch,
resumable page validation with canonical height-rule replay, dense cold/warm graph
walks and recovery dependency discovery.

Debug evidence: all eight owned source files pass Clang 23 syntax checks. Scoped
page-validator fixtures pass for 1, 32, 256 and 1,000 definitions with one-byte,
one-row budgets; hash counts show one page SHA and zero on already-compared
validation. Scoped dense reads pass exact starts, redirected verified references,
malformed framing, back edges and bounded body reads. Graph fixtures pass 0, 1,
32, 256 and 1,000 definitions, forged summaries, reachability and cleanup.

Remaining: migrate prepared-reader disk fixture and lazy read expectations; run
composed reader, recovery, warm validation and sanitizer suites. Whole-feature
review and acceptance measurements remain lead gates.
