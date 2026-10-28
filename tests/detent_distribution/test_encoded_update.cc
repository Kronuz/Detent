#include "detent_distribution/encoded_update.hh"
#include "detent_distribution/framing.hh"
#include "detent_distribution/wire_codec.hh"
#include "detent_distribution/update_cache.hh"
#include <iostream>
#include <source_location>
namespace d = detent::distribution;
namespace m = msgpack::bounded;
namespace {
void check(bool condition, std::source_location where = std::source_location::current()) {
	if (!condition)
		throw std::runtime_error("encoded update invariant at " + std::to_string(where.line()));
}
d::Cursor cursor(std::uint64_t revision) {
	d::Cursor result;
	result.cluster.fill('c');
	result.group.fill('g');
	result.restore_epoch.fill('e');
	result.scope.fill('s');
	result.revision = revision;
	return result;
}
void put_cursor(std::string &out, d::Cursor id) {
	m::AppendPacker(out).pack_array(5);
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::AppendPacker(out).pack_bin(std::string_view(field->data(), field->size()));
	m::AppendPacker(out).pack_bin(std::string_view(id.scope.data(), id.scope.size()));
	m::AppendPacker(out).pack_uint64(id.revision);
}
std::string delta(bool progress, std::uint64_t base = 42, std::uint64_t target = 45) {
	std::string out;
	m::AppendPacker(out).pack_array(progress ? 5 : 7);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(progress ? 3 : 2);
	m::AppendPacker(out).pack_array(2);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(1);
	put_cursor(out, cursor(base));
	put_cursor(out, cursor(target));
	if (!progress) {
		m::AppendPacker(out).pack_array(4);
		m::AppendPacker(out).pack_uint64(1);
		m::AppendPacker(out).pack_uint64(1);
		m::AppendPacker(out).pack_uint64(0);
		m::AppendPacker(out).pack_uint64(8);
		m::AppendPacker(out).pack_array(1);
		m::AppendPacker(out).pack_array(3);
		m::AppendPacker(out).pack_uint64(target - 1);
		m::AppendPacker(out).pack_array(1);
		m::AppendPacker(out).pack_array(2);
		m::AppendPacker(out).pack_str("app");
		m::AppendPacker(out).pack_uint64(target - 1);
		m::AppendPacker(out).pack_array(1);
		m::AppendPacker(out).pack_array(4);
		m::AppendPacker(out).pack_uint64(0);
		m::AppendPacker(out).pack_str("app:x");
		m::AppendPacker(out).pack_uint64(1);
		out.push_back('\xc0');
	}
	return out;
}

std::string snapshot() {
	std::string out;
	m::AppendPacker(out).pack_array(7);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_array(2);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_array(5);
	const auto id = cursor(42);
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::AppendPacker(out).pack_bin(std::string_view(field->data(), field->size()));
	m::AppendPacker(out).pack_bin(std::string_view(id.scope.data(), id.scope.size()));
	m::AppendPacker(out).pack_uint64(id.revision);
	m::AppendPacker(out).pack_array(4);
	m::AppendPacker(out).pack_uint64(1);
	m::AppendPacker(out).pack_uint64(3000);
	m::AppendPacker(out).pack_uint64(0);
	m::AppendPacker(out).pack_uint64(3000 * 34 + 3);
	m::AppendPacker(out).pack_array(1);
	m::AppendPacker(out).pack_array(2);
	m::AppendPacker(out).pack_str("app");
	m::AppendPacker(out).pack_uint64(42);
	m::AppendPacker(out).pack_array(3000);
	for (unsigned i = 0; i < 3000; ++i) {
		m::AppendPacker(out).pack_array(4);
		m::AppendPacker(out).pack_uint64(0);
		char key[35];
		std::snprintf(key, sizeof(key), "app:%030u", i);
		m::AppendPacker(out).pack_str(key);
		m::AppendPacker(out).pack_uint64(42);
		m::AppendPacker(out).pack_array(2);
		m::AppendPacker(out).pack_uint64(1);
		m::AppendPacker(out).pack_int64(i);
	}
	return out;
}
d::ValidatedSnapshot decode(std::string_view bytes, std::shared_ptr<d::StorageLedger> account) {
	d::Limits limits;
	limits.storage_account = account;
	d::SnapshotDecoder decoder(cursor(42), {"app"}, limits);
	auto input = std::span(bytes.data(), bytes.size());
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	return decoder.finish();
}
std::string decode_frames(const d::EncodedUpdate &update, bool compressed,
						  std::shared_ptr<d::StorageLedger> account) {
	d::FragmentDecoder decoder(update.kind(), update.logical_bytes(), compressed, account);
	std::string result;
	for (std::size_t i = 0; i < update.fragments(); ++i) {
		auto frame = update.frame(i, compressed);
		auto input = std::span(frame->view().data(), frame->size());
		while (!input.empty()) {
			d::WorkBudget budget{37, 1};
			decoder.feed(input, budget);
			if (auto fragment = decoder.take()) {
				check(fragment->offset == result.size() && fragment->update == update.identity());
				check(fragment->final == (i + 1 == update.fragments()));
				result.append(fragment->bytes());
			}
		}
	}
	decoder.finish();
	return result;
}
d::RetirementHandle<d::EncodedUpdate> stage_delta(d::RetirementDomain &domain,
												  d::RetirementDomain &input_domain,
												  std::shared_ptr<d::StorageLedger> input_account,
												  std::uint64_t base, std::uint64_t target) {
	const auto wire = delta(false, base, target);
	d::Limits limits;
	limits.storage_account = input_account;
	d::DeltaDecoder decoder(cursor(base), {"app"}, limits);
	auto input = std::span(wire.data(), wire.size());
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	auto stage = domain.make<d::UpdateStage>(
		domain, d::make_delta_source(decoder.finish(), 1, input_domain, domain.storage_account()),
		cursor(42).cluster);
	while (!stage->done()) {
		d::WorkBudget budget;
		stage->step(budget);
	}
	return stage->take();
}

} // namespace
int main() {
	try {
		auto root = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
		auto cache = std::make_shared<d::StorageLedger>(root, 24 * 1024 * 1024);
		auto publication = std::make_shared<d::StorageLedger>(root, 24 * 1024 * 1024);
		auto baseline = std::make_shared<d::StorageLedger>(root, 16 * 1024 * 1024);
		{
			d::RetirementRuntime runtime(baseline, 2);
			{
				auto domain = runtime.domain(cache, 16);
				auto input_domain = runtime.domain(publication, 16);
				const auto input_resident = publication->used();
				const auto resident = cache->used();
				const auto wire = snapshot();
				auto input = decode(wire, publication);
				d::Identity identity;
				identity.fill('u');
				auto stage = domain.make<d::UpdateStage>(
					domain, d::make_snapshot_source(std::move(input), input_domain, cache), identity);
				unsigned turns = 0;
				while (!stage->done()) {
					d::WorkBudget budget{1, 1};
					stage->step(budget);
					check(++turns < 20000000);
				}
				input_domain.close().wait();
				check(publication->used() == input_resident);
				auto update = stage->take();
				stage.reset();
				check(update->sealed() && update->fragments() > 1 && update->target() == cursor(42));
				check(decode_frames(*update, false, publication) == wire);
				check(decode_frames(*update, true, publication) == wire);
				auto original = update->frame(0, false);
				update.reset();
				// Closing waits for object cleanup; an accepted original keeps its own bytes charged.
				auto drain = domain.close();
				drain.wait();
				check(cache->used() > resident && !original->view().empty());
				original.reset();
				check(cache->used() == resident);
			}
			{
				auto domain = runtime.domain(cache, 64);
				auto input_domain = runtime.domain(publication, 64);
				auto window = d::make_storage_owner<d::UpdateCache>(cache, std::malloc, std::free,
																	cursor(42).cluster, 2);
				check(window->offer({}).decision == d::CacheDecision::Busy);
				window->select_publication(cursor(42));
				check(window->offer(cursor(42)).decision == d::CacheDecision::Idle);
				auto first = stage_delta(domain, input_domain, publication, 42, 45);
				window->select_publication(cursor(45), first);
				const auto mark = window->marker();
				auto held = window->offer(cursor(42));
				check(held.decision == d::CacheDecision::Update && held.update.get() == first.get());
				check(window->covers(mark, cursor(42)));
				check(window->offer(cursor(43)).decision == d::CacheDecision::SnapshotRequired);
				check(window->offer(cursor(48)).decision == d::CacheDecision::SourceBehind);
				check(window->offer(cursor(45), true).decision == d::CacheDecision::Busy);
				window->select_publication(cursor(48),
										   stage_delta(domain, input_domain, publication, 45, 48));
				window->select_publication(cursor(51),
										   stage_delta(domain, input_domain, publication, 48, 51));
				check(window->retained_edges() == 2 && !window->covers(mark, cursor(42)));
				check(window->offer(cursor(42)).decision == d::CacheDecision::SnapshotRequired);
				check(decode_frames(*held.update, false, publication) == delta(false));
				window->select_publication(cursor(54));
				check(window->retained_edges() == 0 && !window->covers(mark, cursor(51)));
				check(window->offer(cursor(51)).decision == d::CacheDecision::SnapshotRequired);
				check(window->offer(cursor(54), true).decision == d::CacheDecision::Busy);
				for (;;) {
					d::WorkBudget budget{1, 1};
					if (window->release_stale(budget))
						break;
				}
				check(window->retained_edges() == 0);
				window->fence();
				check(window->offer(cursor(54)).decision == d::CacheDecision::Fenced);
				held.update.reset();
				first.reset();
				window.reset();
				domain.close().wait();
				input_domain.close().wait();
			}

			for (bool progress : {false, true}) {
				auto domain = runtime.domain(cache, 16);
				auto input_domain = runtime.domain(publication, 16);
				const auto wire = delta(progress);
				d::Limits limits;
				limits.storage_account = publication;
				d::DeltaDecoder decoder(cursor(42), {"app"}, limits,
										progress ? d::UpdateKind::Progress : d::UpdateKind::Delta);
				auto remaining = std::span(wire.data(), wire.size());
				while (!remaining.empty()) {
					d::WorkBudget budget;
					decoder.feed(remaining, budget);
				}
				auto input = decoder.finish();
				auto stage = domain.make<d::UpdateStage>(
					domain, d::make_delta_source(std::move(input), 1, input_domain, cache),
					cursor(42).cluster);
				unsigned turns = 0;
				while (!stage->done()) {
					d::WorkBudget budget{1, 1};
					stage->step(budget);
					check(++turns < 2000000);
				}
				auto update = stage->take();
				check(update->base() == cursor(42) && update->target() == cursor(45));
				check(update->kind() == (progress ? d::UpdateKind::Progress : d::UpdateKind::Delta));
				check(decode_frames(*update, false, publication) == wire);
				check(decode_frames(*update, true, publication) == wire);
				update.reset();
				stage.reset();
				domain.close().wait();
				input_domain.close().wait();
			}
			{
				auto domain = runtime.domain(cache, 16);
				auto input_domain = runtime.domain(publication, 16);
				auto input = std::make_shared<d::ValidatedSnapshot>(decode(snapshot(), publication));
				std::weak_ptr<const d::ValidatedSnapshot> weak = input;
				auto stage = domain.make<d::UpdateStage>(
					domain,
					d::make_snapshot_source(std::shared_ptr<const d::ValidatedSnapshot>(input), cache),
					cursor(42).cluster);
				for (unsigned turn = 0; turn < 100; ++turn) {
					d::WorkBudget budget{1, 1};
					stage->step(budget);
				}
				auto reader = weak.lock();
				stage.reset();
				domain.close().wait();
				check(reader && reader->cursor() == cursor(42) && reader->records().size() == 3000);
				reader.reset();
				for (;;) {
					d::WorkBudget budget{1, 1};
					if (input->discard(budget))
						break;
				}
				input.reset();
				input_domain.close().wait();
			}
			{
				auto input_domain = runtime.domain(publication, 1);
				auto wrong_domain = runtime.domain(cache, 1);
				auto input = decode(snapshot(), publication);
				bool rejected = false;
				try {
					d::make_snapshot_source(std::move(input), wrong_domain, cache);
				} catch (const std::invalid_argument &) {
					rejected = true;
				}
				check(rejected && input.cursor() == cursor(42) && input.records().size() == 3000);
				auto source = d::make_snapshot_source(std::move(input), input_domain, cache);
				auto another = decode(snapshot(), publication);
				rejected = false;
				try {
					d::make_snapshot_source(std::move(another), input_domain, cache);
				} catch (const d::StoragePressure &) {
					rejected = true;
				}
				check(rejected && another.cursor() == cursor(42) && another.records().size() == 3000);
				for (;;) {
					d::WorkBudget budget;
					if (another.discard(budget))
						break;
				}
				source.reset();
				input_domain.close().wait();
				wrong_domain.close().wait();
			}
			{
				auto input_domain = runtime.domain(publication, 1);
				const auto resident = publication->used();
				auto input = decode(snapshot(), publication);
				auto zero = std::make_shared<d::StorageLedger>(0);
				bool rejected = false;
				try {
					d::make_snapshot_source(std::move(input), input_domain, zero);
				} catch (const d::StoragePressure &) {
					rejected = true;
				}
				check(rejected);
				input_domain.close().wait();
				check(publication->used() == resident);
			}

			for (unsigned stop : {0, 100, 100000, 250000}) {
				auto input_account = std::make_shared<d::StorageLedger>(publication, publication->limit());
				auto domain = runtime.domain(cache, 16);
				auto input_domain = runtime.domain(input_account, 16);
				const auto input_resident = input_account->used();
				auto input = decode(snapshot(), input_account);
				auto stage = domain.make<d::UpdateStage>(
					domain, d::make_snapshot_source(std::move(input), input_domain, cache),
					cursor(42).cluster);
				for (unsigned turn = 0; turn < stop && !stage->done(); ++turn) {
					d::WorkBudget budget{17, 1};
					stage->step(budget);
				}
				stage.reset();
				domain.close().wait();
				input_domain.close().wait();
				check(input_account->used() == input_resident);
			}

			runtime.shutdown();
			check(root->used() == baseline->used() && cache->used() == 0 && publication->used() == 0);
		}
		check(root->used() == 0 && baseline->used() == 0);
		std::cout << "encoded update checks passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
