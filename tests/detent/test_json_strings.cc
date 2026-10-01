#include "detent/json_schema.hh"
#include "detent/json_patch.hh"
#include <cassert>
#include <string>

auto source(std::string_view bytes) { return detent::parse_source_snapshot(bytes, "test"); }
auto patch(std::string_view bytes) { return detent::parse_snapshot_patch(bytes, "test"); }

int main() {
    assert(!source(R"({"schema_version":1,"version":1,"knobs":{"a:b\u0000hidden":{"type":"int","default":3}}})"));
    assert(!source(R"({"schema_version\u0000hidden":1,"version":1,"knobs":{}})"));
    assert(!detent::parse_defaults(R"({"schema_version":1,"knobs":{"a:b":{"type":"bool\u0000hidden","default":false}}})"));
    assert(!patch(R"({"schema_version":1,"version":1,"knobs":{"a:b\u0000hidden":null}})"));
    auto parsed = source(R"({"schema_version":1,"version":1,"knobs":{"a:b":{"type":"bool","default":false,"overrides":{"beta\u0000hidden":true}}}})");
    assert(parsed);
    auto group = std::string("beta\0hidden", 11);
    assert(std::get<detent::BoolDef>(parsed.value().knobs.at("a:b")).overrides.contains(group));
    auto again = source(detent::serialize_snapshot(parsed.value()));
    assert(again && again.value().knobs == parsed.value().knobs);
    auto invalid = std::string(R"({"schema_version":1,"knobs":{"a:b":{"type":"bool","default":false,"overrides":{")") + char(0xff) + R"(":true}}}})";
    assert(!detent::parse_defaults(invalid));
}
