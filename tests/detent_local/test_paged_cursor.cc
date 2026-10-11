#include "paged_cursor.hh"
#include "paged_disk_fixture.hh"
#include "paged_large_fixture.hh"
namespace p = l::paged;
std::uint64_t walk(l::detail::PagedCursor &cursor, const d::ValidatedSnapshot &expected, p::PageKind kind) {
	std::uint64_t rows = 0;
	std::string previous;
	while (!cursor.done()) {
		unsigned turns = 0;
		while (!cursor.ready() && !cursor.done()) {
			l::ValidationBudget b{1, 1};
			cursor.step(b);
			require(++turns < 10000 && b.bytes <= 1 && b.rows <= 1);
		}
		if (!cursor.ready())
			break;
		const auto leaf = cursor.leaf();
		require(previous.empty() || previous < leaf.key);
		previous = leaf.key;
		const auto key = p::decode_key(leaf.key);
		require(key.kind == kind);
		if (kind == p::PageKind::Namespace) {
			require(rows < expected.namespaces().size() &&
					key.name_space == expected.namespaces()[rows].name);
		} else {
			require(rows < expected.records().size());
			const auto &record = expected.records()[rows];
			require(key.name_space == expected.namespaces()[record.namespace_index].name);
			require(record.key == std::string(key.name_space) + ':' + std::string(key.local_name));
		}
		++rows;
		cursor.consume();
	}
	return rows;
}
int main() {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-cursor-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code e;
			std::filesystem::remove_all(path, e);
		}
	} cleanup{directory};
	for (unsigned count : {2u, 20000u}) {
		auto source =
			std::make_shared<const d::ValidatedSnapshot>(decode(count == 2 ? fixture() : many(count)));
		l::StorageIdentity storage{}, generation{};
		storage.fill('s');
		generation.fill('g');
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		while (!builder.done()) {
			l::ValidationBudget b;
			builder.step(b);
		}
		auto image = builder.take();
		install(directory, image, storage);
		auto recovered = std::optional<l::RecoveredGeneration>(l::recover_generation(directory));
		const auto baseline = source->storage_account()->used();
		{
			l::detail::PagedCursor records(source->storage_account(), *recovered, p::PageKind::Definition);
			l::ValidationBudget zero{0, 0};
			records.step(zero);
			require(!records.ready());
			require(walk(records, *source, p::PageKind::Definition) == source->records().size());
			records.rewind();
			l::detail::PagedCursor namespaces(source->storage_account(), *recovered, p::PageKind::Namespace);
			recovered.reset();
			std::filesystem::remove_all(directory);
			require(walk(namespaces, *source, p::PageKind::Namespace) == source->namespaces().size());
			require(walk(records, *source, p::PageKind::Definition) == source->records().size());
			l::ValidationBudget no_rows{10000, 0};
			namespaces.rewind();
			namespaces.step(no_rows);
			require(!namespaces.ready());
			l::ValidationBudget only_rows{0, 1};
			namespaces.step(only_rows);
			require(only_rows.rows == 0);
		}
		require(source->storage_account()->used() == baseline);
	}
}
