#include "dense_fixture.hh"
#include "detent_local/paged_definition.hh"
#include <iostream>
using namespace dense_test;
int main() {
	const auto empty_digest =
		digest("316b4053cea6792530f8c441e83ecb3dfb497cf8afba9ac30759a5bc885e9acc");
	require(p::empty_commitment() == empty_digest);
	p::validate(p::Reference{});
	p::Reference r;
	r.segment.fill('s');
	r.offset = 0x1020;
	r.length = 0x1234;
	r.keys = 0x102030405060708ull;
	r.height = 5;
	for (unsigned i = 0; i < 32; ++i)
		r.digest[i] = i;
	const auto exact_content =
		unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f "
			  "0102030405060708 00001234 05 000000");
	const auto exact_placement = unhex("73737373737373737373737373737373 00001020");
	require(std::ranges::equal(p::encode_content(r), exact_content));
	require(std::ranges::equal(p::encode_placement(r), exact_placement));
	p::validate(r);
	rejects([&] { p::encode_placement(p::Reference{}); });
	for (auto offset : {1u, p::segment_bytes, UINT32_MAX}) {
		auto bad = r;
		bad.offset = offset;
		rejects([&] { p::validate(bad); });
		rejects([&] { p::encode_placement(bad); });
	}
	for (auto length : {0u, 78u, p::page_bytes + 1, UINT32_MAX}) {
		auto bad = r;
		bad.length = length;
		rejects([&] { p::validate(bad); });
		rejects([&] { p::encode_content(bad); });
	}
	{
		auto bad = r;
		bad.segment = {};
		rejects([&] { p::validate(bad); });
	}
	auto root = empty_root();
	auto exact_root = unhex("4454524f4f543033 00000003 00000140 "
							"0000000000000000 00000000 00000000 0000000000000000 "
							"0000000000000000 0000000000000000 "
							"0000000000000011 63636363636363636363636363636363 "
							"67676767676767676767676767676767 "
							"65656565656565656565656565656565 "
							"7373737373737373737373737373737373737373737373737373737373737373 "
							"00000001 00000000 0000000000000001");
	exact_root.resize(320);
	require(std::ranges::equal(p::encode_root(root, p::Limits{}), exact_root));
	require(p::state_commitment(p::decode_root(exact_root, p::Limits{})) == empty_digest);
	auto page = watermark();
	auto ref = reference(page);
	p::verify_page(page, ref, p::Limits{});
	const auto exact_page =
		unhex("4454504147453033 0000004f 0000 0001 0000000000000001 01 00000000000000 "
			  "0000000000000000000000000000000000000000000000000000000000000000 "
			  "03 03 0008 576100 0000000000000011");
	auto expected_page = exact_page;
	const auto expected_digest =
		digest("fb64e273fec41c4270b4fdbb7323570be428ae4660eaeb88d819fdf2fa411599");
	std::copy(expected_digest.begin(), expected_digest.end(), expected_page.begin() + 32);
	require(page == expected_page && ref.digest == expected_digest);
	root.namespaces = root.pages = root.segments = 1;
	root.bytes = page.size();
	root.tree = ref;
	auto nonempty = exact_root;
	l::write_integer(nonempty, 16, 79, 8);
	l::write_integer(nonempty, 28, 1, 4);
	l::write_integer(nonempty, 32, 1, 8);
	l::write_integer(nonempty, 40, 1, 8);
	// The tree reference is one child entry: content [160,208), then placement [208,228).
	const auto ref_bytes =
		unhex("fb64e273fec41c4270b4fdbb7323570be428ae4660eaeb88d819fdf2fa411599 "
			  "0000000000000001 0000004f 01 000000 73737373737373737373737373737373 00000000");
	require(ref_bytes.size() == p::child_bytes);
	std::copy(ref_bytes.begin(), ref_bytes.end(), nonempty.begin() + 160);
	require(std::ranges::equal(p::encode_root(root, p::Limits{}), nonempty));
	require(p::decode_root(nonempty, p::Limits{}).tree == ref && p::state_commitment(root) == ref.digest);
	{
		const auto content = p::encode_content(ref);
		const auto placement = p::encode_placement(ref);
		require(std::equal(content.begin(), content.end(), nonempty.begin() + 160) &&
				std::equal(placement.begin(), placement.end(), nonempty.begin() + 208));
	}
	for (auto state : {empty_root(), root, empty_root(), root}) {
		const auto decoded = p::decode_root(p::encode_root(state, p::Limits{}), p::Limits{});
		require(decoded.tree == state.tree && decoded.cursor == state.cursor &&
				decoded.profile == state.profile &&
				p::state_commitment(decoded) == p::state_commitment(state));
	}
	for (std::size_t i = 0; i < 320; ++i) {
		if ((i >= 48 && i < 56) || (i >= 148 && i < 152) || (i >= 205 && i < 208) || i >= 228) {
			for (const auto &seed : {exact_root, nonempty}) {
				auto bad = seed;
				bad[i] = 1;
				rejects([&] { p::decode_root(bad, p::Limits{}); });
			}
		}
	}
	for (std::size_t i = 160; i < 160 + p::child_bytes; ++i) {
		auto bad = exact_root;
		bad[i] = 1;
		rejects([&] { p::decode_root(bad, p::Limits{}); });
	}
	for (std::size_t i = 25; i < 32; ++i) {
		auto bad = page;
		bad[i] = 1;
		rejects([&] { p::decode_page_header(bad, p::Limits{}); });
	}
	for (auto field : {8u, 12u, 14u, 16u, 24u, 64u, 65u, 66u}) {
		auto bad = page;
		bad[field] = static_cast<char>(255);
		rejects([&] {
			reseal(bad);
			p::verify_page(bad, reference(bad), p::Limits{});
		});
	}
	for (auto &summary : {ref}) {
		auto bad = summary;
		++bad.keys;
		rejects([&] { p::verify_page(page, bad, p::Limits{}); });
		bad = summary;
		++bad.height;
		rejects([&] { p::verify_page(page, bad, p::Limits{}); });
		bad = summary;
		++bad.length;
		rejects([&] { p::verify_page(page, bad, p::Limits{}); });
	}
	for (std::size_t i = 0; i < page.size(); ++i) {
		auto bad = page;
		bad[i] ^= 1;
		rejects([&] { p::verify_page(bad, ref, p::Limits{}); });
	}
	// A root or page in any other format is invalid input, whatever its version says.
	for (const char version : {'2', '4', '9'}) {
		auto other = nonempty;
		other[7] = version;
		rejects([&] { p::decode_root(other, p::Limits{}); });
		l::write_integer(other, 8, version - '0', 4);
		rejects([&] { p::decode_root(other, p::Limits{}); });
		other = page;
		other[7] = version;
		rejects([&] { p::decode_page_header(other, p::Limits{}); });
		rejects([&] { p::verify_page(other, ref, p::Limits{}); });
	}
	auto parent = external_branch();
	const auto parent_ref = reference(parent);
	for (auto index : {0u, 1u}) {
		const auto content = parent.size() - 2 * p::child_bytes + index * p::content_bytes;
		for (auto value : {0u, 78u, 65537u, UINT32_MAX}) {
			auto bad = parent;
			l::write_integer(bad, content + 40, value, 4);
			reseal(bad);
			rejects([&] { p::verify_page(bad, reference(bad), p::Limits{}); });
		}
		for (auto height : {0u, 255u}) {
			auto bad = parent;
			bad[content + 44] = static_cast<char>(height);
			reseal(bad);
			rejects([&] { p::verify_page(bad, reference(bad), p::Limits{}); });
		}
		auto bad = parent;
		l::write_integer(bad, content + 32, 2, 8);
		reseal(bad);
		rejects([&] { p::verify_page(bad, reference(bad), p::Limits{}); });
		l::write_integer(bad, 16, 4, 8);
		reseal(bad);
		p::verify_page(bad, reference(bad), p::Limits{});
		const auto false_keys = p::decode_external(bad, index, p::Limits{});
		rejects([&] { p::verify_page(page, false_keys, p::Limits{}); });
		bad = parent;
		bad[content + 44] = 2;
		bad[24] = 3;
		reseal(bad);
		p::verify_page(bad, reference(bad), p::Limits{});
		const auto false_height = p::decode_external(bad, index, p::Limits{});
		rejects([&] { p::verify_page(page, false_height, p::Limits{}); });
		for (std::size_t reserved = 45; reserved < 48; ++reserved) {
			bad = parent;
			bad[content + reserved] = 1;
			reseal(bad);
			rejects([&] { p::verify_page(bad, reference(bad), p::Limits{}); });
		}
	}
	for (auto field : {12u, 13u, 70u, 72u, 74u}) {
		auto bad = parent;
		bad[field] = static_cast<char>(255);
		rejects([&] {
			reseal(bad);
			p::verify_page(bad, reference(bad), p::Limits{});
		});
	}
	auto moved = parent;
	l::write_integer(moved, moved.size() - 4, 88, 4);
	require(p::page_commitment(moved, p::Limits{}) == parent_ref.digest);
	p::verify_page(moved, parent_ref, p::Limits{});
	p::reset_hash_counts();
	p::verify_page_structure(parent, parent_ref, p::Limits{});
	require(p::hash_counts() == p::HashCounts{});
	p::verify_page(parent, parent_ref, p::Limits{});
	require(p::hash_counts() == p::HashCounts{1, parent.size() - 32 - 40, 0, 0, 0, 0});
	p::pack_commitment(parent);
	p::metadata_commitment(parent);
	require(p::hash_counts() ==
			p::HashCounts{1, parent.size() - 32 - 40, 1, parent.size(), 1, parent.size()});
	std::array<char, 202> key{};
	auto n = p::encode_definition_key(key, "ns:knob");
	require(p::decode_key({key.data(), n}).local_name == "knob");
	n = p::encode_namespace_key(key, "ns");
	require(p::decode_key({key.data(), n}).name_space == "ns");
	rejects([&] { p::decode_key(std::string_view("Kns\0\0", 5)); });
	detent::KnobDef def{INT64_MIN};
	std::vector<char> body(p::definition_size(def));
	p::encode_definition(body, def, 17);
	require(p::validate_definition(body, 17).default_value == INT64_MIN);
	rejects([&] { p::validate_definition(body, 16); });
	std::cout << "exact version-3 reference/root/page fixtures and malformed "
				 "framing passed\n";
}
