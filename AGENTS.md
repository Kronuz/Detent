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

The Astro site lives in `site/`; `docs/` holds engineering designs and campaign
records. The pinned `@kronuz/starlight-core` package supplies shared
components, Markdown plugins, themes, and styles to all four sites. Detent owns
its content, navigation, logo, footer identity, and Astro configuration. The
reading controls come from `Kronuz.github.io:297524c` and
`Kronuz.github.io:37728df`. The reading CSS scales Expressive Code's
`--ec-codeFontSize`; the edit link serves every docs entry. The local-editor
URL inserts a slash after Astro's `/Detent` base path.

- **One filled sidebar link.** The existing sidebar gives `aria-current="page"`
  only to the current page; its custom sublist needs no ancestor conversion.
- **Reading size.** The shared theme selector puts Aa before the theme selector on
  desktop; `Search.astro` puts it after search on phones. Exactly one is
  visible on a docs page, and splash pages hide both. The toggle stores
  `kz-text-size`; route middleware restores it from `<head>` before paint.
  Inside `<main>`, Starlight's type tokens grow by 1.125; the content-width
  token grows from 45rem to 48.6rem at the root. Lato needs this
  narrower scale to keep lines near 98 characters. The header and sidebars
  keep their size. The 300px TOC minimum caps the panel at 761px at a
  1440px viewport; the toggle preserves the reading point.
- **Image zoom.** The shared Markdown wrapper mounts the desktop-only modal. A
  content image is zoomable only if the fitted modal enlarges it by at least
  1.15x. Linked images, snippets, code, and touch input are left alone.
- **Unified footer.** The shared footer renders copyright, edit metadata,
  pagination, social links, and "Proudly hosted by GitHub" in one centered row.
- **Author edit links.** The shared edit link renders "Edit source" in the footer,
  with a local-editor icon in development. In production, `?editor=on` opts
  in and `?editor=off` opts out; the `kz-editor` choice persists across pages.
  The title row holds only the title.
- **Code copy on touch screens.** The shared Markdown wrapper mounts
  `CopyReveal.astro` (`Kronuz.github.io:480b250`). Tapping a code block
  reveals its Expressive Code Copy button; tapping the block again hides it.
  The CSS keeps Copy hidden until
  that tap or keyboard focus. Desktop hover retains Expressive Code's behavior.
