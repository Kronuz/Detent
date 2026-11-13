#include "paged_disk_fixture.hh"
#include "paged_large_fixture.hh"
#include "paged_reuse.hh"
#include <fcntl.h>
#include <sys/file.h>
namespace p = l::paged;
template <class Error, class F> void rejects(F action) {
	try {
		action();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid contextual reuse accepted");
}
std::optional<l::detail::ContextualReuse> finish(l::detail::ReuseNavigator &navigator) {
	unsigned turns = 0;
	while (!navigator.done()) {
		l::ValidationBudget budget{1, 1};
		navigator.step(budget);
		require(++turns < 10000 && budget.bytes <= 1 && budget.rows <= 1);
	}
	auto result = navigator.take();
	rejects<std::logic_error>([&] { navigator.take(); });
	return result;
}
int main() {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-reuse-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	auto account = source->storage_account();
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, generation);
	while (!builder.done()) {
		l::ValidationBudget b;
		builder.step(b);
	}
	auto image = builder.take();
	install(directory, image, storage);
	auto recovered = std::optional<l::RecoveredGeneration>(l::recover_generation(directory));
	const auto descriptor = recovered->descriptor();
	const auto identity = recovered->directory_identity();
	std::array<char, 202> key{};
	auto size = p::encode_definition_key(key, "app:count");
	const std::string_view target(key.data(), size);
	const auto baseline = account->used();
	auto navigator = l::detail::PagedPlanBuilder::reuse(*recovered, account, target);
	l::ValidationBudget none{0, 0};
	navigator.step(none);
	require(!navigator.done());
	auto proof = finish(navigator);
	require(proof && proof->reference() == image.root().tree && proof->header().keys == image.root().tree.keys);
	require(proof->file_identity().inode != 0);
	require(proof->matches(proof->reference(), proof->entering_prefix(), descriptor, identity));
	{
		auto exact = l::detail::PagedPlanBuilder::reuse_at(*recovered, account, proof->reference(),
														   proof->entering_prefix());
		auto checked = finish(exact);
		require(checked && checked->matches(proof->reference(), proof->entering_prefix(), descriptor, identity));
		auto wrong = proof->reference();
		wrong.digest[0] ^= 1;
		auto rejected =
			l::detail::PagedPlanBuilder::reuse_at(*recovered, account, wrong, proof->entering_prefix());
		require(!finish(rejected));
		auto misplaced = l::detail::PagedPlanBuilder::reuse_at(*recovered, account, proof->reference(), "wrong-context");
		require(!finish(misplaced));
		require(proof->entering_prefix().empty());
		auto misplaced_inline = l::detail::PagedPlanBuilder::reuse_at(
			*recovered, account, proof->reference(), target.substr(0, 1));
		require(!finish(misplaced_inline));
		auto extended =
			l::detail::PagedPlanBuilder::reuse_at(*recovered, account, proof->reference(), target);
		require(!finish(extended));
	}
	auto bad_ref = proof->reference();
	++bad_ref.offset;
	require(!proof->matches(bad_ref, proof->entering_prefix(), descriptor, identity));
	require(!proof->matches(proof->reference(), "wrong-context", descriptor, identity));
	auto bad_descriptor = descriptor;
	bad_descriptor.checksum[0] ^= 1;
	require(!proof->matches(proof->reference(), proof->entering_prefix(), bad_descriptor, identity));
	auto bad_identity = identity;
	++bad_identity.inode;
	require(!proof->matches(proof->reference(), proof->entering_prefix(), descriptor, bad_identity));
	{
		auto root = l::detail::PagedPlanBuilder::reuse(*recovered, account, target, 0);
		auto frontier = finish(root);
		require(frontier && frontier->reference() == image.root().tree &&
				frontier->entering_prefix().empty());
		require(frontier->header().keys == image.root().records + image.root().namespaces);
		auto exact = l::detail::PagedPlanBuilder::reuse_at(*recovered, account, frontier->reference(), "");
		auto checked = finish(exact);
		require(checked && checked->reference() == frontier->reference());
	}
	{
		auto branch = l::detail::PagedPlanBuilder::reuse(*recovered, account, target, 1);
		auto frontier = finish(branch);
		require(frontier && frontier->reference() == image.root().tree &&
				frontier->entering_prefix().empty());
		auto exact = l::detail::PagedPlanBuilder::reuse_at(*recovered, account, frontier->reference(), "");
		auto checked = finish(exact);
		require(checked && checked->reference() == frontier->reference());
	}
	{
		auto length = p::encode_namespace_key(key, "app");
		auto ns = l::detail::PagedPlanBuilder::reuse(*recovered, account, {key.data(), length});
		auto frontier = finish(ns);
		require(frontier && frontier->reference() == image.root().tree && frontier->entering_prefix().empty());
	}
	{
		auto length = p::encode_definition_key(key, "app:missing");
		auto absent = l::detail::PagedPlanBuilder::reuse(*recovered, account, {key.data(), length});
		require(!finish(absent));
	}
	rejects<std::invalid_argument>([&] { l::detail::PagedPlanBuilder::reuse(*recovered, account, "Kbad"); });
	p::encode_definition_key(key, "app:count");
	auto pressure = account->admit(account->limit() - account->used());
	rejects<d::StoragePressure>([&] { l::detail::PagedPlanBuilder::reuse(*recovered, account, target); });
	pressure.reset();
	require(!navigator.discard(none));
	l::ValidationBudget disposal{0, 1};
	require(navigator.discard(disposal));
	require(account->used() == baseline);
	{
		const auto flat_id = l::next_generation(std::nullopt, source->cursor(), source->profile(), true).id;
		l::ImageBuilder flat(source);
		while (!flat.done()) {
			l::ValidationBudget b;
			flat.step(b);
		}
		auto name = l::generation_name(flat_id);
		write(directory / (name + ".image"), *flat.bytes());
		l::GenerationOwnership ownership{storage, flat_id, flat.descriptor().length,
										 flat.descriptor().checksum};
		write(directory / (name + ".lease"), l::encode_generation_ownership(ownership));
		write(directory / "selected", l::encode_descriptor(flat.descriptor()));
		auto legacy = l::recover_generation(directory);
		rejects<std::invalid_argument>([&] { l::detail::PagedPlanBuilder::reuse(legacy, account, target); });
	}
	{
		auto large = std::make_shared<const d::ValidatedSnapshot>(decode(many(50000)));
		l::GenerationId id{};
		id.fill('m');
		l::detail::PagedSnapshotBuilder build(large, id);
		while (!build.done()) {
			l::ValidationBudget b;
			build.step(b);
		}
		auto image = build.take();
		require(image.segments().size() > 1);
		auto path = directory / "multi";
		install(path, image, storage);
		auto base = l::recover_generation(path);
		auto length = p::encode_definition_key(key, "app:k019999");
		auto navigation =
			l::detail::PagedPlanBuilder::reuse(base, large->storage_account(), {key.data(), length});
		auto frontier = finish(navigation);
		require(frontier && frontier->header().keys > 0 &&
				frontier->matches(frontier->reference(), frontier->entering_prefix(), base.descriptor(),
								  base.directory_identity()));
		require(base.view().get("app", "k019999").value() == 19999);
	}
	recovered.reset();
	auto anchor = directory / (l::generation_name(generation) + ".lease");
	int fd = ::open(anchor.c_str(), O_RDONLY | O_CLOEXEC);
	require(fd >= 0);
	require(::flock(fd, LOCK_EX | LOCK_NB) != 0);
	require(::close(fd) == 0);
	std::filesystem::remove_all(directory);
	require(proof->matches(proof->reference(), proof->entering_prefix(), descriptor, identity));
	auto moved = std::move(*proof);
	rejects<std::logic_error>([&] { proof->reference(); });
	require(moved.header().keys == image.root().tree.keys);
}
