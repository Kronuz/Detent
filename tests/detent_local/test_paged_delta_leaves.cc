#include "applied_delta_fixture.hh"
#include "paged_delta_leaves.hh"
#include "paged_disk_fixture.hh"
#include "paged_overlay_assembler.hh"
#include "paged_overlay_fixture.hh"
#include "paged_segment_builder.hh"
#include "paged_warm_validator.hh"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
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
	auto image = std::make_shared<l::detail::PagedImage>(builder.take());
	install(directory, *image, storage);
	auto recovered = l::recover_generation(directory);
	std::vector<l::detail::SegmentInput> segments;
	for (const auto &segment : image->segments())
		segments.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
	std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
		return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
	});
	std::vector<l::detail::SegmentReachability> live;
	{
		l::detail::GraphValidator graph(source->storage_account(), image, segments, image->root(), storage);
		while (!graph.done()) {
			l::ValidationBudget work;
			graph.step(work);
		}
		for (std::size_t i = 0; i < segments.size(); ++i)
			live.push_back(graph.segment_reachability(i));
	}
	for (bool progress : {false, true})
		for (unsigned transactions : {1u, 2u, 17u}) {
			auto update = delta(progress, transactions);
			auto account = update->storage_account();
			const auto baseline = account->used();
			{
				l::detail::DeltaLeaves leaves(recovered, update);
				l::detail::TrieOverlay overlay(account, source, segments, image->root());
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
				l::GenerationId next{};
				next.fill('n');
				l::detail::OverlayAssembler assembly(account, overlay, segments, live, image->root(), storage,
													 next, update->target());
				for (unsigned turn = 0; !assembly.done(); ++turn) {
					require(turn < 2000000);
					l::ValidationBudget work{1, 1};
					assembly.step(work);
				}
				auto assembled = std::make_shared<l::detail::PagedImage>(assembly.take());
				require(assembled->root().logical == wanted.root().logical &&
						assembled->root().records == wanted.root().records);
				require(assembled->descriptor().cursor == update->target() &&
						assembled->descriptor().generation == next);
				require(progress ? assembled->segments().empty() : assembled->segments().size() == 1);
				std::vector<l::detail::SegmentInput> merged;
				for (std::size_t i = 0; i < segments.size(); ++i)
					if (live[i].pages != overlay.removed()[i].pages)
						merged.push_back(segments[i]);
				for (const auto &segment : assembled->segments())
					merged.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
				std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) {
					return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
				});
				{
					l::detail::GraphValidator graph(account, assembled, merged, assembled->root(), storage);
					for (unsigned turn = 0; !graph.done(); ++turn) {
						require(turn < 2000000);
						l::ValidationBudget work{1, 1};
						graph.step(work);
					}
					require(graph.root().logical == wanted.root().logical &&
							graph.counts().ints == (transactions == 1 && !progress ? 0 : 1));
				}
				{
					std::vector<l::detail::SegmentInput> added;
					std::uint64_t bytes = 0;
					for (const auto &segment : assembled->segments()) {
						added.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
						bytes += segment.bytes->size();
					}
					l::detail::WarmGraphValidator warm(account, recovered, assembled, added,
													   assembled->root());
					for (unsigned turn = 0; !warm.done(); ++turn) {
						require(turn < 2000000);
						l::ValidationBudget work{1, 1};
						warm.step(work);
					}
					require(warm.root().logical == wanted.root().logical &&
							warm.hashed_segment_bytes() == bytes);
					require(warm.counts().ints == (transactions == 1 && !progress ? 0 : 1) &&
							warm.counts().bools == 1);
					for (std::size_t i = 0; i < segments.size(); ++i)
						require(warm.removed(i).pages == overlay.removed()[i].pages &&
								warm.removed(i).bytes == overlay.removed()[i].bytes);
					{
						l::detail::WarmGraphValidator cancelled(account, recovered, assembled, added,
																assembled->root());
						for (unsigned turn = 0; turn < 2500; ++turn) {
							l::ValidationBudget work{1, 1};
							cancelled.step(work);
						}
						require(!cancelled.done());
						l::ValidationBudget none{0, 0};
						require(!cancelled.discard(none));
						for (unsigned turn = 0;; ++turn) {
							require(turn < 2000000);
							l::ValidationBudget work{1, 1};
							if (cancelled.discard(work))
								break;
						}
					}
					if (!progress) {
						auto bad = assembled->root();
						++bad.bytes;
						l::detail::WarmGraphValidator invalid(account, recovered, assembled, added, bad);
						bool rejected = false;
						try {
							for (unsigned turn = 0; !invalid.done(); ++turn) {
								require(turn < 2000000);
								l::ValidationBudget work{1, 1};
								invalid.step(work);
							}
						} catch (const std::invalid_argument &) {
							rejected = true;
						}
						require(rejected);
						for (unsigned turn = 0;; ++turn) {
							require(turn < 2000000);
							l::ValidationBudget work{1, 1};
							if (invalid.discard(work))
								break;
						}
					}
					for (unsigned turn = 0;; ++turn) {
						require(turn < 2000000);
						l::ValidationBudget work{1, 1};
						if (warm.discard(work))
							break;
					}
				}
				if (!progress) {
					auto limits = p::Limits{};
					limits.segments = 1;
					const auto path = directory / ("mapped-" + std::to_string(transactions));
					const auto &segment = assembled->segments()[0];
					write(path, {segment.bytes->data(), segment.bytes->size()});
					d::RetirementRuntime runtime(account, 1);
					auto domain = runtime.domain(account, 4);
					{
						auto registry = domain.make<l::detail::SegmentRegistry>(account, storage, limits);
						const auto fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
						require(fd >= 0);
						registry->append(domain.make<l::detail::SegmentMapping>(account, fd, ::getuid(),
																				segment.ownership));
						require(::close(fd) == 0);
						registry->seal();
						auto owner =
							std::make_shared<d::RetirementHandle<l::detail::SegmentRegistry>>(registry);
						l::detail::WarmGraphValidator warm(account, recovered, owner, registry->inputs(),
														   assembled->root());
						for (unsigned turn = 0; !warm.done(); ++turn) {
							require(turn < 2000000);
							l::ValidationBudget work{1, 1};
							warm.step(work);
						}
						bool unavailable = false;
						try {
							registry->validated_root();
						} catch (const std::logic_error &) {
							unavailable = true;
						}
						require(unavailable);
						l::ValidationBudget none{0, 0};
						require(!warm.record_reachability(*registry, none));
						for (unsigned turn = 0;; ++turn) {
							require(turn < 2000000);
							l::ValidationBudget work{1, 1};
							if (warm.record_reachability(*registry, work))
								break;
						}
						require(registry->validated_root().logical == assembled->root().logical &&
								registry->reachability(0).pages == assembled->root().pages);
						require(registry->override_histogram()[0] == assembled->root().records);
						require(warm.record_reachability(*registry, none));
						for (unsigned turn = 0;; ++turn) {
							require(turn < 2000000);
							l::ValidationBudget work{1, 1};
							if (warm.discard(work))
								break;
						}
					}
					domain.close().wait();
				}
				for (unsigned turn = 0;; ++turn) {
					require(turn < 2000000);
					l::ValidationBudget work{1, 1};
					if (assembly.discard(work))
						break;
				}
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
	{
		auto account = source->storage_account();
		const std::array<p::Child, 2> children{{{'K', image->root().tree}, {'W', image->root().tree}}};
		l::detail::PageBuilder page(account, p::PageKind::Branch, 6, "", children, {});
		while (!page.done()) {
			l::ValidationBudget work;
			page.step(work);
		}
		l::GenerationId next{};
		next.fill('a');
		l::detail::SegmentBuilder segment(account, storage, next, 0);
		segment.append(page);
		while (!segment.page_ready()) {
			l::ValidationBudget work;
			segment.step(work);
		}
		const auto reference = segment.reference();
		segment.seal();
		while (!segment.done()) {
			l::ValidationBudget work;
			segment.step(work);
		}
		std::vector<l::detail::SegmentInput> added{
			{segment.ownership(), {segment.bytes()->data(), segment.bytes()->size()}}};
		auto bad = image->root();
		++bad.cursor.revision;
		bad.tree = reference;
		bad.logical = reference.logical;
		bad.records = 5;
		++bad.pages;
		bad.bytes += reference.length;
		++bad.segments;
		p::encode_root(bad);
		l::detail::WarmGraphValidator validator(account, recovered, segment.bytes(), added, bad);
		bool rejected = false;
		try {
			for (unsigned turn = 0; !validator.done(); ++turn) {
				require(turn < 2000000);
				l::ValidationBudget work{1, 1};
				validator.step(work);
			}
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		require(rejected);
		for (unsigned turn = 0;; ++turn) {
			require(turn < 2000000);
			l::ValidationBudget work{1, 1};
			if (validator.discard(work))
				break;
		}
	}
	std::cout << "sparse changed leaves match independent full-snapshot histories and bounded cancellation\n";
}
