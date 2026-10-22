#include <btree/map.h>
#include <cstdio>
#include <stdexcept>
struct Value {
    inline static int live = 0, budget = -1;
    int number;
    explicit Value(int input) : number(input) { ++live; }
    Value(const Value& other) : number(other.number) {
        if (budget == 0) throw std::runtime_error("value copy refused");
        if (budget > 0) --budget;
        ++live;
    }
    Value(Value&& other) : Value(static_cast<const Value&>(other)) {}
    Value& operator=(const Value&) = default;
    ~Value() { --live; }
};
int main() {
    btree::map<int, Value> source;
    source.emplace(1, 1); source.emplace(2, 2); source.emplace(3, 3);
    const auto before = Value::live;
    Value::budget = 2;
    bool refused = false;
    try { auto copy = source; }
    catch (const std::runtime_error&) { refused = true; }
    Value::budget = -1;
    std::printf("refused=%d extra_live_values=%d\n", refused, Value::live - before);
    return refused && Value::live == before ? 0 : 1;
}
