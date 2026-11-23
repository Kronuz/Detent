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
		const std::array<std::string_view, 1> patterns{"app.*"};
		r::ConfiguredCredential credential;
		class SHA256 hash;
		hash.add(token.data(), token.size());
		hash.getHash(credential.token_hash.data());
		credential.principal = {d::canonical_scope_identity("reader", 1, {}), 1};
		credential.namespaces = std::make_shared<const d::NamespaceGrants>(patterns);
		{
			r::Registry registry(account, {&credential, 1});
			const auto *authenticated = registry.authenticate(bearer);
			check(authenticated && authenticated->principal == credential.principal);
			check(authenticated->namespaces->allows("app.one") && !authenticated->namespaces->allows("app"));
			check(!registry.authenticate("bearer " + token));
			check(!registry.authenticate("Bearer short"));
			check(!registry.authenticate("Bearer 0123456789abcdef0123456789abcdeg"));
			std::atomic<bool> valid{true};
			std::array<std::thread, 8> threads;
			for (auto &thread : threads)
				thread = std::thread([&] {
					for (unsigned i = 0; i < 10000; ++i)
						if (registry.authenticate(bearer) != authenticated)
							valid.store(false);
				});
			for (auto &thread : threads)
				thread.join();
			check(valid.load());
		}
		check(account->used() == 0);
		auto rejected = [&](std::span<const r::ConfiguredCredential> credentials) {
			bool failed = false;
			try {
				r::Registry registry(account, credentials);
			} catch (const std::invalid_argument &) {
				failed = true;
			}
			check(failed && account->used() == 0);
		};
		rejected({});
		const std::array<r::ConfiguredCredential, 2> duplicate{credential, credential};
		rejected(duplicate);
		auto ungranted = credential;
		ungranted.namespaces = std::make_shared<const d::NamespaceGrants>();
		rejected({&ungranted, 1});
		ungranted.namespaces.reset();
		rejected({&ungranted, 1});
		auto anonymous = credential;
		anonymous.principal.identity = {};
		rejected({&anonymous, 1});
		anonymous = credential;
		anonymous.principal.policy_epoch = 0;
		rejected({&anonymous, 1});
		auto tiny = std::make_shared<d::StorageLedger>(64);
		bool refused = false;
		try {
			r::Registry registry(tiny, {&credential, 1});
		} catch (const d::StoragePressure &) {
			refused = true;
		}
		check(refused && tiny->used() == 0);
		std::cout << "immutable relay credentials, namespace grants and concurrent authentication passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
