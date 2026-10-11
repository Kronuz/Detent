#include "applied_delta_fixture.hh"
#include "paged_delta_leaves.hh"
#include "paged_disk_fixture.hh"
#include "paged_overlay_fixture.hh"
#include <algorithm>
#include <cstring>
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-delta-leaves-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path directory;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(directory, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget work;
		builder.step(work);
	}
	auto image = builder.take();
	install(directory, image, storage);
	auto recovered = l::recover_generation(directory);
	std::vector<l::detail::SegmentInput> segments;
	for (const auto &segment : image.segments())
		segments.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
	std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
		return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
	});
	for (bool progress : {false, true})
		for (unsigned transactions : {1u, 2u, 17u}) {
			auto update = delta(progress, transactions);
			auto account = update->storage_account();
			const auto baseline = account->used();
			{
				l::detail::DeltaLeaves leaves(recovered, update);
				l::detail::TrieOverlay overlay(account, source, segments, image.root());
				unsigned emitted = 0, turns = 0;
				l::ValidationBudget none{0, 0};
				leaves.step(none);
				require(!leaves.done() && !leaves.ready());
				while (!leaves.done()) {
					l::ValidationBudget work{1, 1};
					leaves.step(work);
					require(++turns < 2000000);
					if (!leaves.ready())
						continue;
					const auto leaf = leaves.leaf();
					const auto key = p::decode_key(leaf.key);
					if (key.kind == p::PageKind::Namespace) {
						require(leaf.body && l::read_integer(*leaf.body, 0, 8) == 42 + transactions);
					} else if (leaf.body) {
						const auto definition = p::validate_definition(*leaf.body, 42 + transactions);
						require(definition.revision == 42 + transactions);
					}
					overlay.change(leaf.key, leaf.body);
					while (!overlay.operation_done()) {
						l::ValidationBudget work{1, 1};
						overlay.step(work);
						require(++turns < 2000000);
					}
					++emitted;
					leaves.consume();
				}
				require(emitted == (progress ? 0 : 3));
				overlay.seal();
				l::detail::PagedSnapshotBuilder oracle(expected(progress, transactions), storage, generation);
				while (!oracle.done()) {
					l::ValidationBudget work;
					oracle.step(work);
				}
				auto wanted = oracle.take();
				const auto [logical, keys] = fingerprint(overlay.root(), segments);
				require(logical == wanted.root().logical &&
						keys == wanted.root().records + wanted.root().namespaces);
				for (unsigned turn = 0;; ++turn) {
					require(turn < 2000000);
					l::ValidationBudget work{1, 1};
					if (leaves.discard(work))
						break;
				}
				for (unsigned turn = 0;; ++turn) {
					require(turn < 2000000);
					l::ValidationBudget work{1, 1};
					if (overlay.discard(work))
						break;
				}
			}
			require(account->used() == baseline);
		}
	{
		auto update = delta(false);
		auto account = update->storage_account();
		const auto baseline = account->used();
		l::detail::DeltaLeaves leaves(recovered, update);
		for (unsigned turn = 0; !leaves.ready(); ++turn) {
			require(turn < 100000);
			l::ValidationBudget work{1, 1};
			leaves.step(work);
		}
		l::ValidationBudget none{0, 0};
		require(!leaves.discard(none));
		for (unsigned turn = 0;; ++turn) {
			require(turn < 2000000);
			l::ValidationBudget work{1, 1};
			if (leaves.discard(work))
				break;
		}
		require(account->used() == baseline);
	}

	{
		auto update = delta(false, 2, "bad");
		auto account = update->storage_account();
		const auto baseline = account->used();
		l::detail::DeltaLeaves leaves(recovered, update);
		bool rejected = false;
		try {
			for (unsigned turn = 0; !leaves.done(); ++turn) {
				require(turn < 100000);
				l::ValidationBudget work{1, 1};
				leaves.step(work);
			}
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		require(rejected);
		for (unsigned turn = 0;; ++turn) {
			require(turn < 2000000);
			l::ValidationBudget work{1, 1};
			if (leaves.discard(work))
				break;
		}
		require(account->used() == baseline);
	}
	{
		auto update = delta(false);
		auto account = update->storage_account();
		const auto baseline = account->used();
		auto pressure = account->admit(account->limit() - baseline);
		bool rejected = false;
		try {
			l::detail::DeltaLeaves leaves(recovered, update);
		} catch (const d::StoragePressure &) {
			rejected = true;
		}
		require(rejected);
		pressure.reset();
		require(account->used() == baseline);
	}
	std::cout << "sparse changed leaves match independent full-snapshot histories and bounded cancellation\n";
}
