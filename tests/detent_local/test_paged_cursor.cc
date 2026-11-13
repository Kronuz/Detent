#include "paged_cursor.hh"
#include "paged_disk_fixture.hh"
#include "paged_large_fixture.hh"
namespace p = l::paged;
std::uint64_t walk(l::detail::PagedCursor &cursor, const d::ValidatedSnapshot &expected, p::PageKind kind,
				   l::ValidationBudget quantum = {1, 1}) {
	std::uint64_t rows = 0;
	std::string previous;
	while (!cursor.done()) {
		unsigned turns = 0;
		while (!cursor.ready() && !cursor.done()) {
			auto b = quantum;
			cursor.step(b);
			require(++turns < 10000 && b.bytes <= quantum.bytes && b.rows <= quantum.rows);
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
	// The largest count puts more keys in the root page than default limits admit.
	constexpr auto large = p::Limits{}.records + p::Limits{}.namespaces;
	for (unsigned count : {2u, 20000u, large}) {
		d::Limits decoding;
		p::Limits paging;
		auto viewing = l::ViewLimits{};
		l::ValidationBudget quantum{1, 1};
		if (count == large) {
			decoding.records = paging.records = count;
			decoding.bytes = decoding.storage_bytes = paging.bytes = 1024ull * 1024 * 1024;
			viewing = l::ViewLimits::prepared();
			quantum = {8192, 16};
		}
		const auto bytes = count == 2 ? fixture() : many(count);
		d::SnapshotDecoder decoder(cursor(), {"app"}, decoding);
		auto input = std::span(bytes.data(), bytes.size());
		while (!input.empty()) {
			d::WorkBudget budget{65536, 4096};
			decoder.feed(input, budget);
		}
		auto source = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
		l::StorageIdentity storage{}, generation{};
		storage.fill('s');
		generation.fill('g');
		l::detail::PagedSnapshotBuilder builder(source, generation, paging);
		while (!builder.done()) {
			l::ValidationBudget b;
			builder.step(b);
		}
		auto image = builder.take();
		install(directory, image, storage, viewing);
		auto recovered =
			std::optional<l::RecoveredGeneration>(l::recover_generation(directory, viewing));
		const auto baseline = source->storage_account()->used();
		{
			l::detail::PagedCursor records(source->storage_account(), *recovered, p::PageKind::Definition);
			l::ValidationBudget zero{0, 0};
			records.step(zero);
			require(!records.ready());
			require(walk(records, *source, p::PageKind::Definition, quantum) == source->records().size());
			records.rewind();
			l::detail::PagedCursor namespaces(source->storage_account(), *recovered, p::PageKind::Namespace);
			recovered.reset();
			std::filesystem::remove_all(directory);
			require(walk(namespaces, *source, p::PageKind::Namespace, quantum) == source->namespaces().size());
			require(walk(records, *source, p::PageKind::Definition, quantum) == source->records().size());
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
