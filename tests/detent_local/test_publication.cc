#include "detent_local/publication.hh"
#include "snapshot_fixture.hh"
#include <fcntl.h>
#include <filesystem>
#include <sys/file.h>
#include <unistd.h>
namespace l = detent::local;
namespace c = l::completion;
std::unique_ptr<l::ImageBuilder> candidate(std::uint64_t revision, char id) {
	auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decode(fixture(1, 24, 25, revision)));
	l::GenerationId generation;
	generation.fill(id);
	auto builder = std::make_unique<l::ImageBuilder>(snapshot, generation);
	while (!builder->done()) {
		l::ValidationBudget budget;
		builder->step(budget);
	}
	return builder;
}
std::size_t drive(l::ImagePublisher &publisher, std::optional<std::size_t> fail_at = {}) {
	std::size_t originals = 0;
	while ((publisher.state() == l::PublicationState::Preparing ||
			publisher.state() == l::PublicationState::Recovering)) {
		auto operation = publisher.operation();
		require(static_cast<bool>(operation));
		while (!operation->done()) {
			operation->submitted();
			c::Result result;
			if (fail_at && originals == *fail_at) {
				result.token = operation->request().token;
				result.error = std::make_exception_ptr(std::runtime_error("injected original IO failure"));
			} else
				result = kronuz::journal::detail::execute_primitive(operation->io(), operation->request());
			++originals;
			require(operation->complete(std::move(result)));
		}
		publisher.advance();
	}
	return originals;
}
int main() {
	try {
		auto base = candidate(42, 'a'), next = candidate(43, 'b');
		auto root =
			std::filesystem::current_path() / ".scratch" / ("publication-test-" + std::to_string(::getpid()));
		for (std::size_t fault = 0; fault < 13; ++fault) {
			auto directory = root / std::to_string(fault);
			std::filesystem::create_directories(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			auto io = std::make_shared<c::PosixIO>(directory);
			auto admission = std::make_shared<int>(1);
			{
				l::ImagePublisher publisher(io, true, admission);
				publisher.begin(*base);
				require(drive(publisher) == 13);
				require(publisher.state() == l::PublicationState::Published &&
						publisher.published_cursor() == cursor());
			}
			auto recovered = l::recover_generation(directory);
			auto old = recovered.view();
			{
				l::ImagePublisher publisher(io, false, admission, recovered);
				require(!publisher.published_cursor());
				require(drive(publisher) == 4);
				require(publisher.published_cursor() == old.cursor());
				publisher.begin(*next, old.cursor());
				drive(publisher, fault);
				require(publisher.error() != nullptr && publisher.published_cursor() == old.cursor());
				require(publisher.state() ==
						(fault >= 11 ? l::PublicationState::Fenced : l::PublicationState::Rejected));
				auto selected = l::open_generation(directory);
				require(selected.cursor().revision == (fault == 12 ? 43 : 42));
				require(old.cursor().revision == 42);
				if (publisher.state() == l::PublicationState::Fenced) {
					bool rejected = false;
					try {
						publisher.begin(*next, old.cursor());
					} catch (const std::exception &) {
						rejected = true;
					}
					require(rejected);
				}
			}
		}
		auto other_directory = root / "copied-descriptor";
		std::filesystem::create_directories(other_directory);
		std::filesystem::permissions(other_directory, std::filesystem::perms::owner_all);
		std::filesystem::copy_file(root / "1" / "selected", other_directory / "selected");
		auto other_io = std::make_shared<c::PosixIO>(other_directory);
		bool mismatch = false;
		try {
			l::ImagePublisher publisher(other_io, true, std::make_shared<int>(3),
										l::recover_generation(root / "1"));
		} catch (const std::exception &) {
			mismatch = true;
		}
		require(mismatch);
		auto recovery_directory = root / "0";
		auto recovery_io = std::make_shared<c::PosixIO>(recovery_directory);
		auto recovery_admission = std::make_shared<int>(2);
		auto recovery_cap = l::recover_generation(recovery_directory);
		for (std::size_t fault = 0; fault < 4; ++fault) {
			l::ImagePublisher publisher(recovery_io, false, recovery_admission, recovery_cap);
			bool rejected = false;
			try {
				publisher.begin(*next);
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
			drive(publisher, fault);
			require(publisher.state() == l::PublicationState::Fenced && !publisher.published_cursor() &&
					publisher.error());
		}
		{
			l::ImagePublisher publisher(recovery_io, false, recovery_admission, recovery_cap);
			drive(publisher);
			publisher.begin(*next, publisher.published_cursor());
			drive(publisher);
			require(publisher.published_cursor()->revision == 43);
		}
		{
			l::ImagePublisher publisher(recovery_io, false, recovery_admission, recovery_cap);
			require(drive(publisher) == 3);
			require(publisher.state() == l::PublicationState::Fenced && !publisher.published_cursor());
		}
		bool requires_recovery = false;
		try {
			l::ImagePublisher publisher(recovery_io, false, recovery_admission);
		} catch (const std::exception &) {
			requires_recovery = true;
		}
		require(requires_recovery);
		for (std::size_t held_index = 0; held_index < 4; ++held_index) {
			auto lease = std::make_shared<int>(4);
			std::weak_ptr<int> weak = lease;
			std::shared_ptr<c::Operation> held;
			{
				l::ImagePublisher publisher(recovery_io, false, lease,
											l::recover_generation(recovery_directory));
				held = publisher.operation();
				for (std::size_t index = 0; index < held_index; ++index) {
					held->submitted();
					require(held->complete(
						kronuz::journal::detail::execute_primitive(held->io(), held->request())));
				}
				held->submitted();
				publisher.close();
			}
			lease.reset();
			require(!weak.expired());
			bool locked = false;
			try {
				auto owner = recovery_io->acquire_owner(false);
			} catch (const std::exception &) {
				locked = true;
			}
			require(locked);
			int fd = ::open(
				(recovery_directory / (l::generation_name(next->descriptor().generation) + ".lease")).c_str(),
				O_RDONLY | O_CLOEXEC);
			require(fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) != 0);
			require(held->complete(kronuz::journal::detail::execute_primitive(held->io(), held->request())));
			c::drive_synchronously(*held);
			held.reset();
			require(weak.expired());
			require(::flock(fd, LOCK_EX | LOCK_NB) == 0);
			::close(fd);
		}
		for (bool after_completion : {false, true}) {
			l::ImagePublisher publisher(recovery_io, false, recovery_admission,
										l::recover_generation(recovery_directory));
			auto job = publisher.operation();
			for (unsigned index = 0; index < 3; ++index) {
				job->submitted();
				require(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
			}
			job->submitted();
			if (!after_completion)
				publisher.close();
			require(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
			if (after_completion)
				publisher.close();
			publisher.advance();
			require(publisher.state() == l::PublicationState::Closed && !publisher.published_cursor());
		}
		{
			l::ImagePublisher publisher(recovery_io, false, recovery_admission,
										l::recover_generation(recovery_directory));
			auto job = publisher.operation();
			c::drive_synchronously(*job);
			publisher.cancel();
			publisher.advance();
			require(publisher.state() == l::PublicationState::Cancelled && !publisher.published_cursor());
			bool unavailable = false;
			try {
				publisher.begin(*next);
			} catch (const std::exception &) {
				unavailable = true;
			}
			require(unavailable);
		}
		for (unsigned mode = 0; mode < 4; ++mode) {
			auto directory = root / ("publication-close-" + std::to_string(mode));
			std::filesystem::create_directories(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			auto io = std::make_shared<c::PosixIO>(directory);
			auto admission = std::make_shared<int>(5);
			{
				l::ImagePublisher publisher(io, true, admission);
				publisher.begin(*base);
				drive(publisher);
			}
			{
				l::ImagePublisher publisher(io, false, admission, l::recover_generation(directory));
				drive(publisher);
				publisher.begin(*next, publisher.published_cursor());
				while (!std::dynamic_pointer_cast<c::ImagePublication>(publisher.operation())) {
					auto job = publisher.operation();
					c::drive_synchronously(*job);
					publisher.advance();
				}
				auto publication = publisher.operation();
				if (mode == 0) {
					publisher.close();
					require(!publisher.operation() && publisher.state() == l::PublicationState::Closed);
				} else {
					publication->submitted();
					if (mode == 1)
						publisher.cancel();
					require(publication->complete(kronuz::journal::detail::execute_primitive(
						publication->io(), publication->request())));
					if (mode == 2)
						publisher.close();
					c::drive_synchronously(*publication);
					if (mode == 3)
						publisher.close();
					publisher.advance();
					require(publisher.state() ==
							(mode == 1 ? l::PublicationState::Cancelled : l::PublicationState::Closed));
				}
				require(publisher.published_cursor()->revision == 42);
				require(l::open_generation(directory).cursor().revision == (mode == 0 ? 42 : 43));
				bool rejected = false;
				try {
					publisher.begin(*next, publisher.published_cursor());
				} catch (const std::exception &) {
					rejected = true;
				}
				require(rejected);
			}
		}
		auto directory = root / "held";
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto io = std::make_shared<c::PosixIO>(directory);
		auto admission = std::make_shared<int>(1);
		std::weak_ptr<int> weak = admission;
		std::shared_ptr<c::Operation> held;
		{
			l::ImagePublisher publisher(io, true, admission);
			publisher.begin(*base);
			held = publisher.operation();
			held->submitted();
			publisher.cancel();
			bool rejected = false;
			try {
				publisher.advance();
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
		}
		admission.reset();
		require(!weak.expired());
		bool rejected = false;
		try {
			auto lock = io->acquire_owner(false);
		} catch (const std::exception &) {
			rejected = true;
		}
		require(rejected);
		auto completion = kronuz::journal::detail::execute_primitive(held->io(), held->request());
		require(held->complete(std::move(completion)));
		c::drive_synchronously(*held);
		held.reset();
		require(weak.expired());
		{
			auto lock = io->acquire_owner(false);
			require(static_cast<bool>(lock));
		}
		std::filesystem::remove_all(root);
		std::cout << "publication faults, uncertain selection and original leases passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
