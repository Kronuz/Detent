# Snapshot-plan pressure isolation

**Status:** Focused regression passes with both private and canonical resources; hosted CI pending.

The snapshot-plan test fills a shared heap ledger and expects plan construction to refuse admission. Unrelated retirement can release charges after the test captures that ledger's usage, reopening admission. Hosted macOS x86-64 run [40859403284](https://github.com/Kronuz/Detent/actions/runs/40859403284/job/123398919553) fails the `refused && yield_optional` assertion at `Detent:1a7faae`.

The pressure case uses a dedicated child ledger and retirement domain for its source, certificates and plan. It deliberately releases a 1 MiB reservation in the parent after filling the child. That release must not reopen the child's admission. Dropping optional certificates still releases child charges and lets the plan complete with the same cursor and value; selected state and committed disk accounting remain unchanged.

Thirty runs with private-resource archives and thirty with canonical-resource archives pass. The relevant production source bodies are unchanged from main in the private closure; the canonical closure replaces resources as qualified on its isolated branch. A mutant restores the shared-account/domain arrangement while retaining the unrelated-release injection; it fails the expected refusal assertion in both closures. The initial baseline link attempt lacks the publication archive and is a harness failure, not a product failure. This check does not establish that every reported snapshot or distribution flake has the same cause.

No production code or deadline changes. The regression follows the repository's retirement-accounting rule: independent cleanup cannot change the pressure case's admission baseline.
