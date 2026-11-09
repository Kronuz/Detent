#pragma once
#include "detent_local/paged_layout.hh"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <execinfo.h>
#include <iostream>
#include <source_location>
#include <typeinfo>
#include <stdexcept>
#include <string>
#include <vector>
namespace dense_test {
namespace p = detent::local::paged;
namespace l = detent::local;
inline void require(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error(std::string(location.file_name()) + ":" +
								 std::to_string(location.line()) + ": dense page invariant");
}
// An uncaught exception terminates before unwinding, so this handler's backtrace still shows the
// throwing frames; it prints them with the exception's type and message, then aborts.
inline void report_uncaught_exceptions() {
	std::set_terminate([] {
		void *frames[64];
		const auto count = backtrace(frames, 64);
		if (const auto error = std::current_exception()) {
			try {
				std::rethrow_exception(error);
			} catch (const std::exception &e) {
				std::cerr << "uncaught " << typeid(e).name() << ": " << e.what() << '\n';
			} catch (...) {
				std::cerr << "uncaught non-standard exception\n";
			}
		}
		backtrace_symbols_fd(frames, count, 2);
		std::abort();
	});
}
// Format limits that admit a whole corpus: the defaults, raised to its key counts, twice its key
// count in pages and 4 GiB of page bytes.
inline p::Limits corpus_limits(std::uint64_t records, std::uint64_t namespaces) {
	p::Limits limits;
	require(records <= UINT32_MAX && namespaces <= UINT32_MAX);
	limits.records = std::max<std::uint32_t>(limits.records, records);
	limits.namespaces = std::max<std::uint32_t>(limits.namespaces, namespaces);
	limits.pages = std::max<std::uint64_t>(limits.pages, 2 * (records + namespaces));
	limits.bytes = std::max<std::uint64_t>(limits.bytes, 4ull * 1024 * 1024 * 1024);
	return limits;
}
template <class F> void rejects(F operation) {
	try {
		operation();
	} catch (const std::invalid_argument &) {
		return;
	}
	throw std::runtime_error("accepted malformed dense page");
}
inline std::vector<char> unhex(std::string_view text) {
	std::vector<char> out;
	int high = -1;
	for (char c : text) {
		if (c == ' ' || c == '\n' || c == '\t')
			continue;
		const int n = c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10;
		require(n >= 0 && n <= 15);
		if (high < 0)
			high = n;
		else {
			out.push_back(static_cast<char>(16 * high + n));
			high = -1;
		}
	}
	require(high < 0);
	return out;
}
inline p::Digest digest(std::string_view hex) {
	const auto bytes = unhex(hex);
	require(bytes.size() == 32);
	p::Digest result;
	std::copy(bytes.begin(), bytes.end(), result.begin());
	return result;
}
inline p::Root empty_root() {
	p::Root root;
	root.cursor.cluster.fill('c');
	root.cursor.group.fill('g');
	root.cursor.restore_epoch.fill('e');
	root.cursor.scope.fill('s');
	root.cursor.revision = 17;
	return root;
}
inline p::Reference reference(std::span<const char> page) {
	const auto h = p::decode_page_header(page, p::Limits{});
	p::Reference r;
	r.segment.fill('s');
	r.length = h.length;
	r.keys = h.keys;
	r.height = h.height;
	r.digest = h.digest;
	return r;
}
inline void reseal(std::span<char> page) {
	const auto hash = p::page_commitment(page, p::Limits{});
	std::copy(hash.begin(), hash.end(), page.begin() + 32);
}
inline std::vector<char> watermark(std::string_view suffix = std::string_view("Wa\0", 3)) {
	p::PageHeader h;
	h.length = 64 + 4 + suffix.size() + 8;
	const auto header = p::encode_page_header(h, p::Limits{});
	std::vector<char> bytes(header.begin(), header.end());
	bytes.push_back(3);
	bytes.push_back(static_cast<char>(suffix.size()));
	bytes.push_back(0);
	bytes.push_back(8);
	bytes.insert(bytes.end(), suffix.begin(), suffix.end());
	bytes.resize(h.length);
	l::write_integer(bytes, bytes.size() - 8, 17, 8);
	reseal(bytes);
	return bytes;
}
inline std::vector<char> external_branch() {
	auto child = reference(watermark());
	p::PageHeader h{224, 2, 2, 3, 2, {}};
	const auto header = p::encode_page_header(h, p::Limits{});
	std::vector<char> bytes(header.begin(), header.end());
	const auto nodes = unhex("01 00 02 00 01 02 8000 004c 8001 03 00 0008 0000000000000011");
	bytes.insert(bytes.end(), nodes.begin(), nodes.end());
	for (int i = 0; i < 2; ++i) {
		const auto content = p::encode_content(child);
		bytes.insert(bytes.end(), content.begin(), content.end());
	}
	for (int i = 0; i < 2; ++i) {
		child.offset = 80 * i;
		const auto placement = p::encode_placement(child);
		bytes.insert(bytes.end(), placement.begin(), placement.end());
	}
	require(bytes.size() == h.length);
	reseal(bytes);
	p::verify_page(bytes, reference(bytes), p::Limits{});
	return bytes;
}
} // namespace dense_test
