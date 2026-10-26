#include "detent/client.hh"
#include "detent_local/client_source.hh"
#include "detent_local/reader.hh"
#include "lazy_page_access.hh"
#include "names.hh"
#include "paged_read_state.hh"
#include "paged_segment_builder.hh"
#include "prepared_reader.hh"
#include "resources.hh"
#include "segment_names.hh"
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <source_location>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace x = l::detail;
namespace {
void check(bool value, std::source_location at = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("lazy page invariant at " + std::to_string(at.line()));
}
template <class Action> void rejected(Action action) {
	try {
		action();
	} catch (const x::ReadSourceFailure &) {
		return;
	}
	throw std::runtime_error("invalid lazy page accepted");
}
void write_file(const std::filesystem::path &path, std::span<const char> bytes) {
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	check(fd >= 0);
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
		if (written < 0 && errno == EINTR)
			continue;
		check(written > 0);
		offset += written;
	}
	check(::close(fd) == 0);
}
struct Fixture {
	std::filesystem::path path;
	int directory = -1;
	std::shared_ptr<d::StorageLedger> heap = std::make_shared<d::StorageLedger>(32 * p::segment_bytes);
	l::StorageIdentity storage{};
	l::GenerationId generation{};
	p::Root root;
	std::vector<p::Reference> definitions;
	std::vector<std::shared_ptr<const d::OwnedBuffer>> buffers;
	std::vector<x::SegmentInput> segments;
	std::uint64_t physical_bytes = p::root_bytes;
	explicit Fixture(unsigned count = 2) : definitions(count) {
		path = std::filesystem::current_path() /
		       ("lazy-pages-" + std::to_string(::getpid()) + "-" + std::to_string(count));
		check(::mkdir(path.c_str(), 0700) == 0);
		directory = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		check(directory >= 0);
		storage.fill('s');
		generation.fill('g' + count);
		std::vector<p::Reference> namespaces(count);
		auto append = [&](x::SegmentBuilder &segment, p::PageKind kind, std::string_view prefix,
		                  std::span<const p::Child> children, std::span<const char> body) {
			x::PageBuilder page(heap, kind,
			                    children.empty() ? 1 : (children[0].label == 'K' ? count * 2 : count), prefix,
			                    children, body);
			while (!page.done()) {
				l::ValidationBudget budget;
				page.step(budget);
			}
			segment.append(page);
			while (!segment.page_ready()) {
				l::ValidationBudget budget;
				segment.step(budget);
			}
			const auto reference = segment.reference();
			root.bytes += reference.length;
			++root.pages;
			return reference;
		};
		auto save = [&](x::SegmentBuilder &segment) {
			segment.seal();
			while (!segment.done()) {
				l::ValidationBudget budget;
				segment.step(budget);
			}
			const auto ownership = segment.ownership();
			const x::SegmentNames names(ownership.segment);
			write_file(path / names.ownership.data(), l::encode_segment_ownership(ownership));
			const auto bytes = segment.bytes();
			write_file(path / names.payload.data(), {bytes->data(), bytes->size()});
			segments.push_back({ownership, {bytes->data(), bytes->size()}});
			buffers.push_back(bytes);
			physical_bytes += bytes->size();
			++root.segments;
		};
		for (unsigned i = 0; i != count; ++i) {
			x::SegmentBuilder segment(heap, storage, generation, i);
			std::string ns(1, 'a' + i);
			ns += '\0';
			std::array<char, 8> watermark{};
			l::write_integer(watermark, 0, 9, 8);
			namespaces[i] =
			    append(segment, p::PageKind::Namespace, std::string_view(ns).substr(1), {}, watermark);
			detent::BoolDef boolean;
			boolean.default_value = true;
			boolean.overrides.emplace("force", false);
			const detent::KnobDef source = i ? detent::KnobDef(boolean) : detent::KnobDef(detent::IntDef{42});
			std::vector<char> body(p::definition_size(source));
			p::encode_definition(body, source, 8);
			definitions[i] = append(segment, p::PageKind::Definition, ns.substr(1) + 'k' + '\0', {}, body);
			save(segment);
		}
		x::SegmentBuilder index(heap, storage, generation, count);
		std::vector<p::Child> definitions_children, namespace_children;
		for (unsigned i = 0; i != count; ++i) {
			definitions_children.push_back({static_cast<std::uint8_t>('a' + i), definitions[i]});
			namespace_children.push_back({static_cast<std::uint8_t>('a' + i), namespaces[i]});
		}
		const auto definitions_root = append(index, p::PageKind::Branch, {}, definitions_children, {});
		const auto namespace_root = append(index, p::PageKind::Branch, {}, namespace_children, {});
		std::array<p::Child, 2> roots{{{'K', definitions_root}, {'W', namespace_root}}};
		root.tree = append(index, p::PageKind::Branch, {}, roots, {});
		root.logical = root.tree.logical;
		save(index);
		root.records = root.namespaces = count;
		root.cursor.cluster.fill('c');
		root.cursor.group.fill('g');
		root.cursor.restore_epoch.fill('e');
		root.cursor.scope.fill('s');
		root.cursor.revision = 10;
		std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
		x::GraphValidator proof(heap, buffers[0], segments, root, storage);
		while (!proof.done()) {
			l::ValidationBudget budget;
			proof.step(budget);
		}
		const auto image = p::encode_root(root);
		const auto checksum = p::physical_commitment(image);
		l::Descriptor descriptor{generation, p::root_bytes, checksum, root.cursor, root.profile};
		write_file(path / "storage.meta", l::encode_storage_identity(storage));
		write_file(path / x::GenerationName(generation, x::GenerationFile::Image).view(), image);
		write_file(path / x::GenerationName(generation, x::GenerationFile::Lease).view(),
		           l::encode_generation_ownership({storage, generation, p::root_bytes, checksum, 2}));
		x::ReaderMetadata metadata{storage, generation, checksum, proof.counts(), physical_bytes};
		write_file(path / x::GenerationName(generation, ".read").view(), x::encode_reader_metadata(metadata));
		write_file(path / "selected", l::encode_descriptor(descriptor));
	}
	~Fixture() {
		if (directory >= 0)
			::close(directory);
		std::filesystem::remove_all(path);
	}
};
} // namespace
int main() {
	auto fixture = std::make_shared<Fixture>();
	{
		l::ClientSourceOptions source;
		source.directory = fixture->path;
		source.prepared_publisher = ::getuid();
		source.auto_refresh = false;
		detent::InitOptions options;
		options.read_source = l::make_local_read_source(source);
		detent::Client client(options);
		check(client.get("a:k").value() == 42);
	}
	{
		l::GenerationReader reader(fixture->path, {}, {}, ::getuid());
		check(reader.refresh());
		check(reader.read_view().get("a", "k").value() == 42);
		check(!reader.refresh());
		auto prepared = x::PreparedReader::open(fixture->path, ::getuid(), {});
		check(prepared.view.get("a", "k").value() == 42);
		check(prepared.view.eval("b", "k").value() == true);
		check(prepared.view.int_count() == 1);
		check(l::resources::mapping_account({})->used() > 0);
		for (unsigned index = 0; index != 7; ++index) {
			auto bounds = l::ViewLimits::prepared();
			switch (index) {
			case 0:
				bounds.records = 1;
				break;
			case 1:
				bounds.bytes = fixture->physical_bytes - 1;
				break;
			case 2:
				bounds.strings = 0;
				break;
			case 3:
				bounds.namespaces = 1;
				break;
			case 4:
				bounds.overrides = 0;
				break;
			case 5:
				bounds.pages = fixture->root.pages - 1;
				break;
			case 6:
				bounds.segments = fixture->root.segments - 1;
				break;
			}
			rejected([&] { x::PreparedReader::open(fixture->path, ::getuid(), bounds); });
		}
		const auto lease_path =
		    fixture->path / x::GenerationName(fixture->generation, x::GenerationFile::Lease).view();
		const int collector = ::open(lease_path.c_str(), O_RDONLY | O_CLOEXEC);
		check(collector >= 0);
		check(::flock(collector, LOCK_EX | LOCK_NB) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
		check(::close(collector) == 0);
		auto wrong_identity = fixture->root.cursor;
		wrong_identity.scope[0] ^= 1;
		rejected([&] { x::PreparedReader::open(fixture->path, ::getuid(), {}, wrong_identity); });
		auto future = fixture->root.cursor;
		++future.revision;
		rejected([&] { x::PreparedReader::open(fixture->path, ::getuid(), {}, future); });
	}
	d::RetirementRuntime runtime(fixture->heap);
	auto domain = runtime.domain(fixture->heap);
	l::ViewLimits limits;
	limits.bytes = 8 * p::segment_bytes;
	auto mappings = std::make_shared<d::StorageLedger>(8 * p::segment_bytes);
	auto access =
	    domain.make<x::LazyPageAccess>(fixture, fixture->directory, ::getuid(), fixture->storage,
		                               fixture->root, limits, domain.factory(), fixture->heap, mappings);
	auto leaf_reader = [&](std::string_view prefix, std::uint64_t ordinal) {
		std::array<char, 8> watermark{};
		l::write_integer(watermark, 0, 9, 8);
		x::PageBuilder page(fixture->heap, p::PageKind::Namespace, 1, prefix, {}, watermark);
		while (!page.done()) {
			l::ValidationBudget budget;
			page.step(budget);
		}
		x::SegmentBuilder segment(fixture->heap, fixture->storage, fixture->generation, ordinal);
		segment.append(page);
		while (!segment.page_ready()) {
			l::ValidationBudget budget;
			segment.step(budget);
		}
		const auto reference = segment.reference();
		segment.seal();
		while (!segment.done()) {
			l::ValidationBudget budget;
			segment.step(budget);
		}
		const auto ownership = segment.ownership();
		const x::SegmentNames names(ownership.segment);
		write_file(fixture->path / names.ownership.data(), l::encode_segment_ownership(ownership));
		const auto bytes = segment.bytes();
		write_file(fixture->path / names.payload.data(), {bytes->data(), bytes->size()});
		auto root = fixture->root;
		root.tree = reference;
		root.logical = reference.logical;
		root.bytes = reference.length;
		root.pages = root.segments = root.namespaces = 1;
		root.records = 0;
		auto reader =
		    domain.make<x::LazyPageAccess>(fixture, fixture->directory, ::getuid(), fixture->storage, root,
			                               limits, domain.factory(), fixture->heap, mappings);
		using Owner = d::RetirementHandle<x::LazyPageAccess>;
		auto owner = std::make_shared<Owner>(std::move(reader));
		return x::PagedReadState{root, {}, std::shared_ptr<const x::PagedPageAccess>(owner, owner->get())};
	};
	{
		auto namespace_only = leaf_reader(std::string_view("Wa\0", 3), 3);
		check(!namespace_only.definition("a", "k"));
		auto corrupt = leaf_reader(std::string_view("Kx\0k\0", 5), 4);
		rejected([&] { corrupt.definition("a", "k"); });
	}
	check(access->mapped_segments() == 0);
	using Handle = d::RetirementHandle<x::LazyPageAccess>;
	auto owner = std::make_shared<Handle>(access);
	std::shared_ptr<const x::PagedPageAccess> pages(owner, access.get());
	x::PagedReadState state{fixture->root, {}, pages};
	check(state.definition("a", "k")->default_int() == 42);
	check(access->mapped_segments() == 2);
	check(!state.definition("a", "missing"));
	check(access->mapped_segments() == 2);
	check(state.definition("a", "k")->default_int() == 42);
	check(state.definition("b", "k")->default_bool() == true);
	check(access->mapped_segments() == 3);
	auto wrong = fixture->definitions[0];
	wrong.physical[0] ^= 1;
	rejected([&] { access->resolve(wrong); });
	check(!access->available());
	rejected([&] { state.definition("a", "k"); });
	state.access.reset();
	pages.reset();
	owner.reset();
	access.reset();
	domain.close().wait();
	{
		auto bounds = l::ViewLimits::prepared();
		bounds.records = 2;
		const auto page = ::sysconf(_SC_PAGESIZE);
		check(page > 0);
		bounds.mapping_account = std::make_shared<d::StorageLedger>(std::size_t(page) * 2);
		l::GenerationReader reader(fixture->path, bounds, {}, ::getuid());
		check(reader.refresh());
		const auto old_view = reader.read_view();
		check(old_view.get("a", "k").value() == 42);
		auto larger = std::make_shared<Fixture>(3);
		for (const auto &entry : std::filesystem::directory_iterator(larger->path)) {
			if (entry.path().filename() != "selected" && entry.path().filename() != "storage.meta")
				std::filesystem::copy_file(entry.path(), fixture->path / entry.path().filename());
		}
		std::filesystem::copy_file(larger->path / "selected", fixture->path / "selected.next");
		std::filesystem::rename(fixture->path / "selected.next", fixture->path / "selected");
		rejected([&] { reader.refresh(); });
		check(reader.read_view().get("a", "k").value() == 42);
		bounds.records = 3;
		auto replacement = x::PreparedReader::open(fixture->path, ::getuid(), bounds);
		const auto refused = replacement.view.get("b", "k");
		check(!refused && refused.error().category == detent::ErrorCategory::SourceFailure);
		check(old_view.get("a", "k").value() == 42);
		check(bounds.mapping_account->used() <= bounds.mapping_account->limit());
	}
}
