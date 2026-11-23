#!/bin/bash
# Fails when a symbol, file, target or option the Client's removal deleted is named outside historical
# records (legacy-h A10): measurement, review and audit logs, dated architecture records, status ledgers, design records and
# handoffs, the CHANGELOG, benchmark JSON records, patches of past experiments, one_knob_record.cc
# (built only at the commit it records) and the tests that check a removed option is refused.
root=$(git rev-parse --show-toplevel)
pattern='detent::Client|\bClient\(|InitOptions|HealthSnapshot|ClientReadView|EvaluationView|ReadSourceFactory|LocalReadSource|make_local_read_source|make_managed_read_source|ClientSourceOptions|detent::global|TestKnobs|TestGuard|OverrideTable|parse_cli_overrides|SnapshotRef|merge_over_defaults|detail::evaluate|detent/client\.hh|detent/read_view\.hh|detent/global\.hh|detent/testing\.hh|snapshot_ref\.hh|detent/overrides\.hh|client_source\.hh|detent_read_bench|detent_model_lookup|detent_update_bench|local_client|DETENT_DEFAULTS_PATH|defaults_path|source_poll|DetentError|detent_eval|detent_read\(|DETENT_KIND|detent_value|DETENT_WRONG_TYPE|cpp-api/client|cpp-api/sources|global-and-testing|guides/live-updates|06_adapters'
if git -C "$root" grep -nE "$pattern" -- src include tests bindings benchmarks examples tools docs website README.md \
	':!docs/reviews' ':!docs/measurements' ':!docs/perf' ':!docs/status' ':!docs/design' ':!docs/handoffs' ':!CHANGELOG.md' \
	':!docs/dev/docs-site-audit.md' ':!docs/DELIVERY_INTEGRATION_ARCHITECTURE.md' ':!docs/READER_CAPACITY_ARCHITECTURE.md' \
	':!docs/CONTAINER_SELECTION_*.md' ':!docs/DOCUMENTATION_FINDINGS.md' ':!benchmarks/*.json' ':!benchmarks/container-alternative' \
	':!tests/detent/one_knob_record.cc' ':!tests/detent_agent/test_agent_cli.cmake' ':!tests/docs/mutants.py' \
	':!bindings/tests/legacy_grep.sh'; then
	echo "legacy references remain" >&2
	exit 1
fi
echo "no legacy references"
