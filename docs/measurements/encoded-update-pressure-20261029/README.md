# Encoded-update pressure isolation

**Status:** Sixty focused runs pass across private and canonical resource closures; both shared-account mutants fail. Hosted correction qualification is pending.

Main run [40944720188](https://github.com/Kronuz/Detent/actions/runs/40944720188) at Detent:d87b66e fails Linux ASan/UBSan at `test_distribution_encoded_update`, invariant 340. The other five required jobs pass. The same stash pin passes all six complete-component jobs on the capsule candidate in [40944723261](https://github.com/Kronuz/Detent/actions/runs/40944723261).

The failing case captures the shared publication ledger after earlier retirement cases. `close().wait()` drains payload cleanup, but accounting metadata can remain briefly on the worker. An unrelated retirement can reduce the captured parent baseline. The test target links neither logger nor stash; this failure is not evidence against the stash upgrade.

The corrected case uses its own child account and retirement domain for decoded input. An unrelated parent reservation is released after capturing the child baseline. Snapshot-source admission still refuses the zero-capacity target, and the child's final usage equals its baseline. The aggregate runtime cleanup assertion remains unchanged. No production code or deadline changes.

The driver compiles the changed test against existing frozen public-pin private archives and canonical-resource capsule archives. Thirty runs pass per closure. A mutant replaces only the child account with its parent, retaining the unrelated-release injection; both variants fail invariant 343. These focused checks reuse archives and do not replace a fresh complete build or hosted sanitizer run. The initial private link attempt lacks the distribution archive; building that existing target supplies it before the successful checks.

`verify.py` preserves the driver with its repository-root path adjusted for this evidence directory. It expects the retained build directories named in the script. Run it under a light hold after `quiet check`; it compiles two variants per closure and runs sixty checks. Ruff and strict Pyrefly pass for the scratch driver.
