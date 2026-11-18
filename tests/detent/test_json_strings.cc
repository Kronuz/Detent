#include "detent/json_schema.hh"
#include "detent/json_patch.hh"
#include <cassert>
#include <string>

auto source(std::string_view bytes) { return detent::parse_source_snapshot(bytes, "test"); }
auto patch(std::string_view bytes) { return detent::parse_snapshot_patch(bytes, "test"); }

int main() {
    assert(!source(R"({"schema_version":1,"version":1,"knobs":{"a:b\u0000hidden":{"default":3}}})"));
    assert(!source(R"({"schema_version\u0000hidden":1,"version":1,"knobs":{}})"));
    assert(!detent::parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{"default\u0000hidden":0}}})"));
    assert(!patch(R"({"schema_version":1,"version":1,"knobs":{"a:b\u0000hidden":null}})"));
    auto parsed = source(R"({"schema_version":1,"version":1,"knobs":{"a:b":{"default":0,"overrides":{"beta\u0000hidden":1}}}})");
    assert(parsed);
    auto group = std::string("beta\0hidden", 11);
    assert(parsed.value().knobs.at("a:b").overrides.contains(group));
    auto again = source(detent::serialize_snapshot(parsed.value()));
    assert(again && again.value().knobs == parsed.value().knobs);
    auto groups = source(R"({"schema_version":1,"version":2,"knobs":{"a:b":{"default":0,"overrides":{"beta":{"rollout_pct":50},"employees":1},"rollout_value":1}}})");
    assert(groups);
    auto roundtrip = source(detent::serialize_snapshot(groups.value()));
    assert(roundtrip && roundtrip.value().knobs == groups.value().knobs);
    auto delta = detent::diff_snapshots(parsed.value(), groups.value());
    auto delta_roundtrip = patch(detent::serialize_patch(delta));
    assert(delta_roundtrip && delta_roundtrip.value().knobs == delta.knobs);
    for (auto value : {"{}", "{\"rollout_pct\":101}", "{\"rollout_pct\":-1}", "{\"rollout_pct\":null}", "{\"rollout_pct\":1.5}", "{\"rollout_pct\":50,\"extra\":true}", "{\"rollout_pct\":50,\"rollout_pct\":60}"}) {
        auto bytes = std::string(R"({"schema_version":1,"version":1,"knobs":{"a:b":{"default":0,"rollout_value":1,"overrides":{"beta":)") + value + "}}}}";
        assert(!source(bytes));
        assert(!patch(bytes));
    }
    auto invalid = std::string(R"({"schema_version":1,"knobs":{"a:b":{"default":0,"overrides":{")") + char(0xff) + R"(":1}}}})";
    assert(!detent::parse_defaults(invalid));
}
