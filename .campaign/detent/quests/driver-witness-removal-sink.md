# driver-witness-removal-sink: consensus: a range whose end has a witness's removal in force goes to its Sink, which holds no entries

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status driver-witness-removal-sink`.

## Outcome

driver-apply-gap judges witness status by the membership in force at a range's last index. The reverse case stays: a witness whose removal is in force at a range's end gets that range sent to its Sink, which holds no entries. After this quest such a range is either delivered from a source that holds it or refused by name, and the apply index never passes what the application received.

## Criteria

- A1 A driver_test case removes a witness and commits a range past the removal; it fails without the fix (--fails-without) and passes with it: jobs.
- A2 Seeds 1-10000 and the suite, TSan and ASan pass: jobs.
- A3 [artifact] DRIVER.md states the witness transitions both ways: `docs/DRIVER.md`

## Notes

Filed 2026-11-19 from driver-apply-gap (fix round 1, consensus:ee03230). High risk: consensus apply safety.
- From driver-apply-gap's re-review (consensus:4b0b214): G1, DRIVER.md:190 claims what the removal case breaks, so fix that line here; G2, a driver_test case that pins the 'last index' rule (judging at the first index or at the commit index must fail it).
