#!/usr/bin/env bash
# Fails when the tree names Meta's JustKnobs or Meta as a company, in text or
# in a rendered diagram (the D2 source and its SVG; PNG text chunks). Scans
# tracked and untracked-but-not-ignored files; .campaign/, CHANGELOG.md (history)
# and this script are excluded.
set -u
cd "$(dirname "$0")/.."

files() { git ls-files -co --exclude-standard | grep -v -e '^\.campaign/' -e '^tools/check_lineage\.sh$' -e '^CHANGELOG\.md$'; }
status=0

report() {  # report LABEL HITS
	if [ -n "$2" ]; then
		echo "lineage: $1"
		echo "$2" | cut -c1-200
		status=1
	fi
}

# The product name in any form: JustKnobs, just knobs, just_knobs.json, --just-knob.
report "product name" "$(files | tr '\n' '\0' | xargs -0 grep -InHiE 'just[ _-]?knobs?' 2>/dev/null)"

# The old metric prefix, in text files only (a PNG's bytes can match by chance).
report "metric prefix" "$(files | grep -v '\.png$' | tr '\n' '\0' | xargs -0 grep -InHIE '\bjk_' 2>/dev/null)"

# Meta as a company: "Meta's", "Meta Platforms" (any case), or the capitalized
# word. Words such as metadata, storage.meta and import.meta never match.
report "Meta as a company" "$(files | tr '\n' '\0' | xargs -0 grep -InHiE \
	"meta['’]s |meta platforms" 2>/dev/null)"
report "Meta as a company (word)" "$(files | tr '\n' '\0' | xargs -0 grep -InHP \
	'(?<![\w.-])Meta(?![\w.:-])' 2>/dev/null)"

# A PNG holds diagram text as pixels; its D2 source and SVG are scanned above,
# and this scans the PNG's embedded text chunks.
report "PNG metadata" "$(files | grep '\.png$' | tr '\n' '\0' | xargs -0 grep -laiE 'just[ _-]?knobs?|meta platforms' 2>/dev/null)"

[ "$status" -eq 0 ] && echo "lineage: clean"
exit "$status"
