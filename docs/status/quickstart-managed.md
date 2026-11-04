> Status: DRAFT (2026-11-04) Ledger for the hands-on tutorial rewrite. Workstream: quickstart-managed (tech writer).

# quickstart-managed

- Branch / worktree: `docs/quickstart-managed` from `Detent:72ed8ee`, at `.claude/worktrees/agent-a0182892652fc7d4c`.
- Done criteria (the lead's brief): `QUICKSTART.md` and the website tutorial pages (`quickstart.md`, `settings-and-flags.md`, `rollouts.md`, `group-overrides.md`, `next.md`, the `index.mdx` hero snippet) teach one loopback recipe: a single-voter `detentd --authority` on 127.0.0.1, knobs published through its transactional API, a managed `detent-agent` over verified TLS, and `detent get` reading the generation. Every command runs on this host exactly as written. The Client's `source_path`/`source_poll` docs stay as they are (step H). The website build job is green.
- State: in progress; building the binaries for the run.

## Done

None yet.

## Next

1. Run the recipe end to end and keep the transcript.
2. Rewrite the pages from the transcript.
3. Build the site locally, push, dispatch `pages.yml`.

## Blocked on

Nothing yet.
