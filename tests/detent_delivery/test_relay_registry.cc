#include "detent/scope.hh"
#include "detent_relay/registry.hh"
#include <atomic>
#include <iostream>
#include <sha256.h>
#include <thread>

namespace d = detent::distribution;
namespace r = detent::relay;
void check(bool value) {
	if (!value)
		throw std::runtime_error("relay registry invariant");
}
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		const std::string token = "0123456789abcdef0123456789abcdef";
		const std::string bearer = "Bearer " + token;
		std::string original = "app";
		std::array<std::string_view, 1> names{original};
		d::Cursor identity;
		identity.cluster.fill('c');
		identity.group.fill('g');
		identity.restore_epoch.fill('e');
		identity.scope = d::canonical_scope_identity("reader", 1, names);
		const d::SubscriptionPrincipal principal{d::canonical_scope_identity("reader", 1, {}), 1};
		std::array<r::RegistryBundle, 2> bundles{{{identity, principal, names}, {identity, principal, {}}}};
		bundles[1].identity.scope = d::canonical_scope_identity("reader", 1, {});
		r::ConfiguredCredential credential;
		class SHA256 hash;
		hash.add(token.data(), token.size());
		hash.getHash(credential.token_hash.data());
		credential.authorization.principal = principal;
		credential.authorization.grants.fill('f');
		credential.authorization.bundles[0] = 3;
		{
			r::Registry registry(account, bundles, {&credential, 1});
			original.assign("wrong");
			const std::array<std::string_view, 1> exact{"app"}, wrong{"other"};
			const auto *authenticated = registry.authenticate(bearer);
			check(authenticated && registry.resolve(*authenticated, identity, exact) == 0);
			check(registry.resolve(*authenticated, bundles[1].identity, {}) == 1);
			check(!registry.resolve(*authenticated, identity, wrong));
			check(!registry.resolve(*authenticated, identity, {}));
			check(!registry.resolve(*authenticated, bundles[1].identity, exact));
			check(!registry.authenticate("bearer " + token));
			check(!registry.authenticate("Bearer short"));
			check(!registry.authenticate("Bearer 0123456789abcdef0123456789abcdeg"));
			auto revoked = *authenticated;
			revoked.bundles = {};
			check(!registry.resolve(revoked, identity, exact));
			auto rotated = *authenticated;
			rotated.principal.policy_epoch = 2;
			check(!registry.resolve(rotated, identity, exact));
			std::atomic<bool> valid{true};
			std::array<std::thread, 8> threads;
			for (auto &thread : threads)
				thread = std::thread([&] {
					for (unsigned i = 0; i < 10000; ++i) {
						const auto *authorization = registry.authenticate(bearer);
						if (!authorization || registry.resolve(*authorization, identity, exact) != 0)
							valid.store(false);
					}
				});
			for (auto &thread : threads)
				thread.join();
			check(valid.load());
		}
		check(account->used() == 0);
		original = "app";
		names[0] = original;
		auto rejected = [&](std::span<const r::RegistryBundle> configured,
							std::span<const r::ConfiguredCredential> credentials) {
			bool failed = false;
			try {
				r::Registry registry(account, configured, credentials);
			} catch (const std::invalid_argument &) {
				failed = true;
			}
			check(failed && account->used() == 0);
		};
		const std::array<r::ConfiguredCredential, 2> duplicate{credential, credential};
		rejected(bundles, duplicate);
		const std::array<r::RegistryBundle, 2> duplicate_binding{bundles[0], bundles[0]};
		rejected(duplicate_binding, {&credential, 1});
		credential.authorization.bundles[3] = std::uint64_t{1} << 63;
		rejected(bundles, {&credential, 1});
		credential.authorization.bundles[3] = 0;
		auto tiny = std::make_shared<d::StorageLedger>(64);
		bool refused = false;
		try {
			r::Registry registry(tiny, bundles, {&credential, 1});
		} catch (const d::StoragePressure &) {
			refused = true;
		}
		check(refused && tiny->used() == 0);
		std::cout << "immutable relay registry, exact routing and concurrent authentication passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
