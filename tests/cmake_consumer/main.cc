#include <detent/client.hh>
#include <detent/json_schema.hh>
int main() {
    auto value = detent::parse_defaults(R"({"schema_version":1,"knobs":{"app:count":{"type":"int","default":7}}})");
    return value && value.value().knobs.size() == 1 ? 0 : 1;
}
