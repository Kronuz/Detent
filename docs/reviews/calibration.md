# Review calibration

## Configured bounds must reach their consumers

The startup-capacity candidate passed 217 native tests while a relay still created its outbound encoder with default limits and segment discovery reapplied its default bound. Review found both defects. A configuration-parser assertion is insufficient: test a real downstream representation above the previous bound and inspect every builder, validator and recovery boundary.

## Holders containing pointers to their own members

The reader helper initially permitted a compiler-generated move while `ViewLimits.retirement_domain` pointed into the original holder. Existing callers did not move it. A source review found no blocker before the lead identified this latent hazard. For every ownership facade that exports member addresses, verify copy/move behavior explicitly; delete those operations or rebind the exported pointers and prove the invariant at compile time.
