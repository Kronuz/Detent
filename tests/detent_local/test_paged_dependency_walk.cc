#include "paged_dependency_walk.hh"
#include "paged_disk_fixture.hh"
struct Source {
	l::detail::PagedImage image;
	std::shared_ptr<const d::OwnedBuffer> replacement;
	bool available = false;
	mutable unsigned lookups = 0;
	explicit Source(l::detail::PagedImage image) : image(std::move(image)) {}
	static std::optional<l::detail::SegmentInput> lookup(const void *owner, const l::StorageIdentity &id) {
		const auto &source = *static_cast<const Source *>(owner);
		++source.lookups;
		if (!source.available)
			return {};
		for (const auto &segment : source.image.segments())
			if (segment.ownership.segment == id) {
				const auto bytes = source.replacement ? source.replacement : segment.bytes;
				return l::detail::SegmentInput{segment.ownership, {bytes->data(), bytes->size()}};
			}
		return {};
	}
};
int main() {
	auto input = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(input, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto source = std::make_shared<Source>(builder.take());
	auto account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	const auto baseline = account->used();
	for (unsigned variant = 0; variant < 4; ++variant) {
		auto root = source->image.root();
		source->available = false;
		source->lookups = 0;
		source->replacement.reset();
		if (variant == 1) {
			++root.pages;
			root.bytes += l::paged::page_header_bytes;
		}
		if (variant == 2) {
			const auto &original = source->image.segments().front();
			auto bytes = d::make_owned_buffer(account, original.bytes->size());
			bytes->resize(original.bytes->size());
			std::copy(original.bytes->data(), original.bytes->data() + original.bytes->size(),
					  bytes->mutable_bytes().begin());
			bytes->mutable_bytes()[root.tree.offset + root.tree.length - 1] ^= 1;
			bytes->seal();
			source->replacement = bytes;
		}
		if (variant == 3) {
			root.bytes = root.pages = root.segments = root.records = root.namespaces = 0;
			root.tree = {};
			root.logical = l::paged::empty_commitment();
		}
		l::detail::PagedDependencyWalk walk(account, source, Source::lookup, root, storage);
		l::ValidationBudget zero{0, 0};
		walk.step(zero);
		require(!walk.done() && !walk.missing() && source->lookups == 0);
		bool rejected = false, supplied = false;
		try {
			for (unsigned turns = 0; !walk.done(); ++turns) {
				require(turns < 1000000);
				l::ValidationBudget budget{1, 1};
				walk.step(budget);
				if (const auto id = walk.missing()) {
					require(!supplied && *id == root.tree.segment);
					source->available = true;
					supplied = true;
				}
			}
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		require(rejected == (variant == 1 || variant == 2));
		if (!rejected)
			require(walk.done() && supplied == (variant != 3));
		require(!walk.discard(zero));
		for (unsigned turns = 0;; ++turns) {
			require(turns < 10000);
			l::ValidationBudget budget{1, 1};
			if (walk.discard(budget))
				break;
		}
		source->replacement.reset();
		require(account->used() == baseline);
	}
}
