# sha256-avx2: sha256 AVX2 kernel for x86_64 without SHA-NI

Risk: normal. History: this landed before the campaign, under the earlier campaign skill, at sha256:07cdc83 (2026-11-07); its ledger `docs/perf/README.md` in Kronuz/sha256 is the record. The quest's repository is `../sha256`. Its machine state is in `state.json`: `campaign status sha256-avx2`.

## Outcome

x86_64 cores without SHA-NI run an AVX2 kernel that extends two blocks' message schedules at once beside the scalar rounds; detection prefers SHA-NI, then AVX2 with BMI2 and YMM state.

## Acceptance criteria

No ledger: the work was done on the owner's word on 2026-11-07 (item 5 of Detent's first-read findings); criteria from the commit message and the docs it changed, each shown by the committed doc as an artifact.

- A1 [artifact] The AVX2 kernel and its detection (CPUID AVX2 and BMI2, OSXSAVE with YMM state in XCR0): `docs/perf/README.md`
- A2 [artifact] On the i9-9980HK 1.35x the portable kernel at 64 B and 1.6x on bulk data (0.352 against 0.216 GB/s at 64 MiB); OpenSSL EVP stays 1.2x to 1.4x faster: `docs/perf/README.md`
- A3 [artifact] Tests, fuzz and bench iterate the new `kernels` list; README and `docs/API.md` name the kernel: `README.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- Owner, 2026-11-07, to the session overseeing Detent: "Feel free to do #5 (AVX2 SHA-256 kernel) in Kronuz/sha256" (the first-read findings note, Detent:fcb5c4b on a laptop-local branch).
- Risk normal: a new hash kernel selected at run time (the auditor's mapping).

## May land early

None.
