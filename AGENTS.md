# AGENTS.md: working in Detent

## Current campaign

The plan, quests and their evidence are in `.campaign/detent/`, run by the `campaign` skill: `campaign map` shows the whole campaign, `campaign status` what each quest still needs. Long CPU runs that need no agent are `campaign job submit` jobs. `docs/status/`, `docs/handoffs/` and `docs/PLAN.md` are the earlier skill's records, kept as history for their results and reasoning.

## Shared library decisions

strata and Xapiand share Kronuz libraries with Detent (`consensus`, `journal`, `completion`,
`reactor`). Decisions they make that touch those libraries are recorded, with their reasons and
how to object, in `docs/SHARED_LIBRARY_DECISIONS.md`. Read it before changing or pinning any of
those libraries, and record agreement or objections there.

## Sharing this machine

Other agents may run on the same machine, from other sessions and projects, and benchmarks may
run on it too; a compile during a measurement corrupts the measurement. Where the `quiet`
command is installed (`command -v quiet`), use it to coordinate; where it is not, work as usual.

Before compiling, running test suites, or measuring anything, run `quiet check`:

- Exit 0: the machine is free.
- Exit 1: a strict hold or insufficient disk. Read and edit while waiting; do not compile, test or measure. `quiet list` shows current holds.
- Exit 2: light holds, high load or low disk. A light `quiet run` queues for its slot.

Wrap builds and test suites in a light hold, run them at nice 19, and use at most four parallel jobs:

```sh
QUIET_LEVEL=light quiet run detent-build 15 "Detent build" -- nice -n 19 cmake --build build -j4
```

Wrap benchmarks, profiles and long fuzz runs in a strict hold. The wrapper waits for other holds and for host load to settle, then runs alone:

```sh
quiet run detent-bench 90 "Detent: what it measures" -- <command>
```

Use realistic deadlines: a hold expires when its process exits or its deadline passes. Do not rely on a pre-build check alone; the light hold keeps an already-running build visible to a later benchmark.

## Retirement accounting checks

`RetirementDomain::close().wait()` waits for payload cleanup. The worker can retain a domain's accounting metadata briefly after signaling drainage. Use a dedicated child account for each case that compares resident bytes or injects admission pressure; unrelated retirement must not change its baseline or reopen its admission. Check aggregate final cleanup after runtime shutdown and release of caller-owned handles.

## Docs reading and navigation

The docs controls come from `Kronuz.github.io:297524c` and
`Kronuz.github.io:37728df`. The shared `TextSizeToggle.astro`, `Search.astro`,
`ImageZoom.astro`, and image-zoom CSS match the blog. The reading CSS uses
Expressive Code's `--ec-codeFontSize`, and the edit link serves any docs
entry. The local-editor URL inserts a slash after Astro's `/Detent`
base path. The existing sidebar already gives each page one filled link, so the
blog's ancestor hint is unnecessary here.

- **One filled sidebar link.** The existing sidebar gives `aria-current="page"`
  only to the current page; its custom sublist needs no ancestor conversion.
- **Reading size.** `ThemeSelect.astro` puts Aa before the theme selector on
  desktop; `Search.astro` puts it after search on phones. Exactly one is
  visible on a docs page, and splash pages hide both. The toggle stores
  `kz-text-size`; route middleware restores it from `<head>` before paint.
  Inside `<main>`, Starlight's type tokens grow by 1.125; the content-width
  token grows from 45rem to 48.6rem at the root. Lato needs this
  narrower scale to keep lines near 98 characters. The header and sidebars
  keep their size. The 300px TOC minimum caps the panel at 761px at a
  1440px viewport; the toggle preserves the reading point.
- **Image zoom.** `MarkdownContent.astro` mounts the desktop-only modal. A
  content image is zoomable only if the fitted modal enlarges it by at least
  1.15x. Linked images, snippets, code, and touch input are left alone.
- **Author edit links.** `EditLink.astro` renders "Edit source" in the footer,
  with a local-editor icon in development. In production, `?editor=on` opts
  in and `?editor=off` opts out; the `kz-editor` choice persists across pages.
  The title row holds only the title.
