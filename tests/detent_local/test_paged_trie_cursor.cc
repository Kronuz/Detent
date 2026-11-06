#include "paged_trie_cursor.hh"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("canonical trie stream invariant");
}
template <class Error, class F> void rejects(F call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid trie cursor operation accepted");
}
struct Source {
	std::vector<std::string> keys;
};
std::size_t key(const void *owner, std::uint64_t ordinal, std::span<char> output) {
	const auto &text = static_cast<const Source *>(owner)->keys.at(ordinal);
	check(text.size() <= output.size());
	std::copy(text.begin(), text.end(), output.begin());
	return text.size();
}
struct Expected {
	p::PageKind kind;
	std::uint64_t ordinal, keys;
	std::string key, prefix;
	std::vector<std::uint8_t> children;
};
void oracle(const Source &source, std::size_t begin, std::size_t end,
            std::vector<Expected> &events, int parent = -1) {
	if (begin == end)
		return;
	const auto &first = source.keys[begin];
	const auto below = static_cast<std::size_t>(parent + 1);
	if (end - begin == 1) {
		events.push_back({first[0] == 'K' ? p::PageKind::Definition : p::PageKind::Namespace,
		                  begin, 1, first, first.substr(below), {}});
		return;
	}
	const auto &last = source.keys[end - 1];
	std::size_t common = 0;
	while (common < first.size() && first[common] == last[common])
		++common;
	std::vector<std::uint8_t> children;
	for (auto next = begin; next < end;) {
		const auto label = static_cast<unsigned char>(source.keys[next][common]);
		auto stop = next + 1;
		while (stop < end && static_cast<unsigned char>(source.keys[stop][common]) == label)
			++stop;
		oracle(source, next, stop, events, static_cast<int>(common));
		children.push_back(label);
		next = stop;
	}
	events.push_back({p::PageKind::Branch, 0, end - begin, first.substr(0, common),
	                  first.substr(below, common - below), std::move(children)});
}
void qualify(std::vector<std::string> keys) {
	auto source = std::make_shared<Source>();
	source->keys = std::move(keys);
	std::vector<Expected> expected;
	oracle(*source, 0, source->keys.size(), expected);
	auto account = std::make_shared<d::StorageLedger>(8 * p::segment_bytes);
	{
		l::detail::TrieCursor cursor(account, source, key, source->keys.size());
		unsigned turns = 0;
		std::size_t emitted = 0;
		while (!cursor.done()) {
			l::ValidationBudget budget{1, 1};
			cursor.step(budget);
			check(budget.bytes <= 1 && budget.rows <= 1 && ++turns < (source->keys.size() + 1) * 4096);
			if (!cursor.ready())
				continue;
			check(emitted < expected.size());
			const auto event = cursor.event();
			const auto &wanted = expected[emitted++];
			check(event.kind == wanted.kind && event.ordinal == wanted.ordinal &&
			      event.keys == wanted.keys && event.key == wanted.key && event.prefix == wanted.prefix &&
			      std::equal(event.children.begin(), event.children.end(), wanted.children.begin(), wanted.children.end()));
			l::ValidationBudget zero{0, 0};
			check(!cursor.commit(zero) && cursor.ready() && cursor.event().prefix == wanted.prefix);
			l::ValidationBudget commit{0, 1};
			check(cursor.commit(commit));
		}
		check(emitted == expected.size());
		rejects<std::logic_error>([&] { cursor.event(); });
		l::ValidationBudget disposal{0, 1};
		check(cursor.discard(disposal));
	}
	check(!account->used());
}

} // namespace
int main() {
	qualify({});
	qualify({std::string("Wns\0", 4)});
	qualify({std::string("Kns\0k\0", 6), std::string("Wns\0", 4)});
	qualify({std::string("Kns\0k\0", 6), std::string("Kns\0kk\0", 7), std::string("Kns\0kz\0", 7),
			 std::string("Wns\0", 4)});
	qualify({std::string("Ka\0x\0", 5), std::string("Ka.b\0x\0", 7), std::string("Wa\0", 3),
			 std::string("Wa.b\0", 5)});
	const std::string long_name(198, 'n');
	qualify({'K' + long_name + std::string("\0k\0", 3), 'W' + long_name + '\0'});
	std::vector<std::string> keys;
	for (unsigned i = 0; i < 1000; ++i) {
		auto digits = std::to_string(i);
		keys.push_back(std::string("Kns\0", 4) + std::string(4 - digits.size(), '0') + digits + '\0');
	}
	keys.push_back(std::string("Wns\0", 4));
	qualify(std::move(keys));
	for (auto bad : {std::vector<std::string>{std::string("Wns\0", 4), std::string("Kns\0k\0", 6)},
					 std::vector<std::string>{std::string("Wns\0", 4), std::string("Wns\0", 4)},
					 std::vector<std::string>{std::string("Qns\0", 4)}}) {
		auto source = std::make_shared<Source>();
		source->keys = std::move(bad);
		auto account = std::make_shared<d::StorageLedger>(p::segment_bytes);
		l::detail::TrieCursor cursor(account, source, key, source->keys.size());
		rejects<std::invalid_argument>([&] {
			while (!cursor.ready()) {
				l::ValidationBudget budget;
				cursor.step(budget);
			}
		});
		check(!cursor.done() && !cursor.ready());
		rejects<std::logic_error>([&] { cursor.event(); });
		l::ValidationBudget disposal{0, 1};
		check(cursor.discard(disposal) && !account->used());
	}
	{
		auto source = std::make_shared<Source>();
		source->keys = {std::string("Kns\0k\0", 6), std::string("Wns\0", 4)};
		auto account = std::make_shared<d::StorageLedger>(p::segment_bytes);
		std::size_t initial, peak;
		{
			l::detail::TrieCursor cursor(account, source, key, source->keys.size());
			initial = account->used();
			while (!cursor.ready()) {
				l::ValidationBudget budget;
				cursor.step(budget);
			}
			peak = account->peak();
			l::ValidationBudget empty{0, 0};
			check(!cursor.discard(empty));
			l::ValidationBudget first{0, 1};
			check(!cursor.discard(first) && !cursor.ready());
			rejects<std::logic_error>([&] { cursor.step(first); });
			for (;;) {
				l::ValidationBudget budget{0, 1};
				if (cursor.discard(budget))
					break;
			}
			check(!account->used());
		}
		auto short_initial = std::make_shared<d::StorageLedger>(initial - 1);
		rejects<d::StoragePressure>(
			[&] { l::detail::TrieCursor cursor(short_initial, source, key, source->keys.size()); });
		check(!short_initial->used());
		auto short_work = std::make_shared<d::StorageLedger>(peak - 1);
		l::detail::TrieCursor cursor(short_work, source, key, source->keys.size());
		rejects<d::StoragePressure>([&] {
			while (!cursor.ready()) {
				l::ValidationBudget budget;
				cursor.step(budget);
			}
		});
		for (;;) {
			l::ValidationBudget budget{0, 1};
			if (cursor.discard(budget))
				break;
		}
		check(!short_work->used());
	}
	std::cout << "streamed canonical trie matches independent event grouping\n";
}
