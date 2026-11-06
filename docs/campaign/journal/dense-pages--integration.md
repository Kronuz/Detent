# dense-pages--integration: journal

State: working
Quest: /Users/kronuz/code/Kronuz/Detent/docs/campaign/quests/dense-pages.md

## Doing

Finish ownership version-3 routing and safe old-format refusal/refetch; assemble the complete feature.

Adopted `phase/dense-pages-integration` at Detent:813c3ea; the original worktree stays parked. Prior contributor stopped at usage limit, with no live process or hold. The generated quest/task worktree is the active writer now.

## Next

1. Merge current complete codec/build/read/delta checkpoints into assembly.
2. Advance generation layout version to 3 and implement narrowly recognized old-format refusal/refetch under owner lock.
3. Add deterministic interrupted-copy/restart tests, update mechanism docs and run final composed proof.
4. Coordinate measured acceptance and whole-feature paladins before architect ruling.

## Blocked on

Nothing. Cross-component changes go through the lead; no per-step review queue.

## Evidence

Ownership version3 publication, prepared/named reads, census and recovery routing are implemented. Ownership decode recognizes old version2 metadata solely for refusal/cleanup; encode cannot produce version2. Exact independently hashed ownership fixture updated; qualification job pending. Safe obsolete-format refetch remains incomplete.

Detent:7618072 ports state commitment and root-prefix framing/tests. Standalone prefix truncation/empty/reserved/old-refusal checks passed historically.

Historical ledger: `docs/status/campaign-lead.md` on `phase/dense-pages-integration`. Historical evidence is scoped to its recorded code; it is not an automatically passing Campaign 2.0 job.

## Rulings I made

Campaign 2.0 adoption keeps one whole-feature landing. All builds/tests/measurements now use commit-pinned `campaign job submit --quest dense-pages --commit HEAD`; task agents must explicitly pass HEAD because the default quest ref is the assembly branch.

## Resumed qualification

Job 261105-211305-158ebe exposed a stale test: segment length 79 is legal with the 64-byte dense header. The exact lower boundary is now 63 rejected, 64 accepted. Production ownership rules are unchanged by this fixture correction.

## Publication blocker resumed

The composed compaction build failed because PagedNamedRoot still called removed physical_commitment for root metadata. It now uses metadata_commitment, matching snapshot and overlay descriptor construction. Targeted named-root/compaction qualification follows at this exact commit. No waiting on an owner decision.

The composed compaction compile exposed the final production physical_commitment caller in disk-store census. Its root checksum comparison now uses metadata_commitment. A repository-wide src/include search finds no remaining old caller. Delta proof 261106-061050-6ac155 passes leaves, overlay and dense assembly; delta-plan immutable graph validation remains open.

Publication artifacts now encode generation leases with paged::format_version, matching the managed-store path. The composed compaction failure in261106-061455-47047e identified the stale version2 literal. Four delta fixtures pass; rerun all five after composing this checkpoint.

## S5 refusal mechanism checkpoint

State: implementing deterministic refusal/refetch/restart proof.

Recovery routes recognized lease version 2 through the paged context and checks the selected root checksum, storage identity and generation ownership before producing retained obsolete-selection evidence. Startup census consumes that evidence, removes only `selected`, and persists absence through its existing directory barrier before Ready. The ordinary exclusive-lease generation collector recognizes v2 framing for cleanup; the pack sweep runs only after remaining roots are understood. Unknown/corrupt v3 bytes stay fenced. No dependency changes.

Lead ruling: preserve trusted-v3 restart's no-page/pack-scan contract. An embedded DTPAGE02 refuses when reached by full validation or first touch; manufactured old bytes under v3 ownership never authorize automatic deselection. Legitimate owned v2 stores refetch. The frozen-v2 fixture generator builds against Detent:785fb34 and validates exact values before exporting fixture bytes. Deterministic proof and security-review questions follow.

Frozen-v2 generation job 261106-063531-1ed91a failed during CMake setup because the job reference environment name is uppercase (`JOB_REF_FIXTURE`). No compiled fixture or test evidence resulted. The corrected job also derives authentic old `.read`/`.live` records through the frozen baseline's test helper.

## Frozen refusal fixture and deterministic restart proof

Job 261106-063837-22f097 passes at frozen Detent:785fb34 with generator reference Detent:4427690. It produces nine files / 3,144 bytes, derives old reader/live records, recovers them with the frozen reader and checks `app:count == INT64_MIN` plus the exact bool evaluation. Root SHA-256: `2873842c45dc99ec596aa79111c55d8c38900ef40dad09c359c4b7c91c736cda`; pack SHA-256: `2ead959d9d80344cc502e20843eca643fe1b8c33d34815dbe68777131e8f3bab`. `dense_legacy_fixture.hh` contains those exact bytes, with no version-2 encoder or serving code.

The new `test_local_dense_refetch` starts separate processes and exits at four concrete cuts: before `Remove(selected)`, after removal before its directory barrier, after durable absence, and after a 13-byte replacement-root write. Restart exercises a complete record snapshot plan, exact values, selected recovery/confirmation, owned-debris reclamation and exact disk-census equality. Separate cases cover bad root checksum, storage/generation mismatches, obsolete-looking root bytes under ownership3, unknown framing, unowned leases, a held owner lock, a held old-reader lease, foreign-file preservation, and embedded-page refusal during full recovery and lazy first touch. Proof execution pending at the committed candidate.

## Final security-review questions

1. Confirm that authenticated ownership2 plus exact DTROOT02/version2/320-byte framing is the intended deselection boundary; ownership3 and unrecognized damage remain fenced with `selected` intact.
2. Confirm that the stable owner lock, selected checksum/ownership rechecks and retained shared generation lease carry enough authority through census/removal/barrier, including failure and cancellation.
3. Confirm that exclusive-lease root cleanup plus a complete segment mark/sweep safely reclaims old packs; a held obsolete root blocks sweep and retains all quota debt until later collection.
4. Confirm the trusted-v3 no-page/pack-scan ruling and the residual for framing-valid writes after verification. This change adds no general damaged-pack repair.

State: waiting on job 261106-064706-acfa0c at candidate Detent:02bbc64. This exact-commit Debug/AppleClang job builds and runs `test_local_dense_refetch` and `test_local_ownership` in slot `dense-refetch`. The one bounded 45-second wait returned pending; no candidate pass is claimed. Frozen fixture proof 261106-063837-22f097 passes. Lead resumes this branch after the candidate job finishes; inspect its output, finish any code/test corrections, and record final evidence before whole-feature security/correctness review.

Candidate job 261106-064706-acfa0c passes ownership but fails the new refetch fixture before publication: `PagedSnapshotPlanBuilder` correctly rejects its snapshot decoder's default heap account because the planner uses the dedicated runtime account. The full-copy fixture now decodes with that exact account; real process cuts, ownership boundaries and assertions are unchanged. Rerun the candidate proof at this correction.

## S5 passing candidate qualification

Job 261106-065403-f3c14e passes at Detent:9ceabbd (Debug, `/usr/bin/clang++`, existing `dense-refetch` slot): `test_local_dense_refetch` and `test_local_ownership`, 2/2. The fixture resource mismatch is corrected; all four real-process restart cuts, exact full-copy values, ownership/fencing cases, held-lease quota debt/reclamation, foreign-file preservation and embedded-page refusal execute. The frozen baseline fixture remains independently qualified by 261106-063837-22f097. Final feature review questions remain the four recorded above. Routing/barrier dying-mutant proof follows at the same candidate.

State: candidate qualification passed; waiting on dying-mutant job 261106-065516-d5a93a. This job pins Detent:9ceabbd, restores the original obsolete-lease routing bug and requires `test_local_dense_refetch` to fail, then skips the absence barrier and requires another failure. It restores production source after each mutant and requires the final refetch test to pass. Raw logs are retained under the job's `out/`. No mutant result is claimed before job completion; lead resumes or records its terminal result.

## Mutation-proof correction

Job 261106-065516-d5a93a fails its proof command because the routing mutation survives: `test_local_dense_refetch` still passes. Selecting the legacy rather than paged resource context does not bypass the shared recovery engine's authenticated ownership2 refusal, which occurs before either reader path. This is an equivalent mutation for the S5 contract, not evidence that the refusal checks are untested. No routing-mutant kill or barrier result is claimed from that job; the script stops before the barrier mutation.

State: preparing one focused semantic mutation proof at passing Detent:9ceabbd. Disable authenticated-v2 refusal so startup fences instead of persisting absence; require the existing real-crash/refetch suite to fail. Independently bypass the directory barrier before Ready, require failure, restore production and require the suite to pass. Qualification 261106-065403-f3c14e remains passing.

Job 261106-070003-0436c9 kills both semantic mutants: disabling authenticated refusal fails the child Ready invariant (and parent exit check); skipping the absence barrier fails exact census/credit equality. The final restored-production run fails with `lock journal owner: Resource temporarily unavailable`. This is a fixture lifetime race: `restart_copy` reopens the directory on the same runtime before its retirement worker necessarily releases the prior NativeOwner, despite DiskStore drainage. The test now ends and drains the first Runtime before constructing the restart Runtime, preserving the intended real restart and every assertion. No production behavior changes. State: rerunning the same focused mutation/restoration proof at this deterministic lifetime correction.

State: waiting on 261106-070134-ecbe03 at Detent:829f828. It repeats the same semantic refusal mutant, absence-barrier mutant and restored-production pass with deterministic runtime release between restarts. Prior mutant failures are recorded above; final success at this corrected test head awaits this job. No production code changes follow the passing Detent:9ceabbd qualification.
