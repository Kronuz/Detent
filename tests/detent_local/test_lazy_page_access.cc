#include "detent/client.hh"
#include "detent_local/client_source.hh"
#include "detent_local/reader.hh"
#include "lazy_page_access.hh"
#include "names.hh"
#include "paged_read_state.hh"
#include "paged_segment_builder.hh"
#include "prepared_reader.hh"
#include "prepared_reader_fixture.hh"
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
using prepared_test::check;
using prepared_test::Fixture;
using prepared_test::write_file;
template <class Action> void rejected(Action action) {
	try {
		action();
	} catch (const x::ReadSourceFailure &) {
		return;
	}
	throw std::runtime_error("invalid lazy page accepted");
}

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
		const auto owner = fixture->segments.front().ownership;
		const x::SegmentNames names(owner.segment);
		const auto path = fixture->path / names.ownership.data();
		const auto saved = fixture->path / "owner.saved";
		std::filesystem::rename(path, saved);
		auto prepared = x::PreparedReader::open(fixture->path, ::getuid(), l::ViewLimits::prepared());
		check(prepared.view.get("a", "k").value() == 42);
		check(prepared.view.eval("b", "k").value() == true);
		bool refused = false;
		try {
			l::recover_generation(fixture->path);
		} catch (const std::exception &) {
			refused = true;
		}
		check(refused);
		std::filesystem::rename(saved, path);
	}
	for (unsigned variant = 0; variant != 6; ++variant) {
		auto damaged = std::make_shared<Fixture>(4 + variant);
		const x::SegmentNames names(damaged->definitions[0].segment);
		const auto path = damaged->path / names.payload.data();
		if (variant == 0)
			std::filesystem::resize_file(path, p::page_header_bytes - 1);
		else if (variant == 1)
			std::filesystem::resize_file(path, p::segment_bytes + 1);
		else if (variant == 2)
			check(::chmod(path.c_str(), 0620) == 0);
		else if (variant == 3)
			std::filesystem::create_hard_link(path, damaged->path / "duplicate");
		else if (variant == 4) {
			const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
			check(fd >= 0);
			char byte = 0;
			const auto offset = damaged->definitions[0].offset;
			check(::pread(fd, &byte, 1, offset) == 1);
			byte ^= 1;
			check(::pwrite(fd, &byte, 1, offset) == 1);
			check(::close(fd) == 0);
		} else {
			const auto saved = damaged->path / "payload.saved";
			std::filesystem::rename(path, saved);
			std::filesystem::create_symlink(saved, path);
		}
		const auto prepared = x::PreparedReader::open(damaged->path, ::getuid(), l::ViewLimits::prepared());
		const auto result = prepared.view.get("a", "k");
		check(!result && result.error().category == detent::ErrorCategory::SourceFailure);
	}
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
        l::GenerationReader flexible(fixture->path, l::ViewLimits::prepared(), {}, ::getuid());
        check(flexible.refresh());
		auto larger = std::make_shared<Fixture>(3);
		for (const auto &entry : std::filesystem::directory_iterator(larger->path)) {
			if (entry.path().filename() != "selected" && entry.path().filename() != "storage.meta")
				std::filesystem::copy_file(entry.path(), fixture->path / entry.path().filename());
		}
		std::filesystem::copy_file(larger->path / "selected", fixture->path / "selected.next");
		std::filesystem::rename(fixture->path / "selected.next", fixture->path / "selected");
		rejected([&] { reader.refresh(); });
        bool conflict = false;
        try { flexible.refresh(); }
        catch (const std::invalid_argument &) { conflict = true; }
        check(conflict && flexible.read_view().get("a", "k").value() == 42);
		check(reader.read_view().get("a", "k").value() == 42);
		bounds.records = 3;
		auto replacement = x::PreparedReader::open(fixture->path, ::getuid(), bounds);
		const auto refused = replacement.view.get("b", "k");
		check(!refused && refused.error().category == detent::ErrorCategory::SourceFailure);
		check(old_view.get("a", "k").value() == 42);
		check(bounds.mapping_account->used() <= bounds.mapping_account->limit());
	}
}
