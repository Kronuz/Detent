// R5: a Tick the Worker refuses with Pressure leaves Core's clock and deadline where they were, so
// Worker::next_deadline() stays at or before the refused Tick's time, and Driver::arm() (driver.h,
// `at = w.next_deadline()`, then only lowered) arms the group's wheel entry in the past. Built
// against a consensus checkout's test/worker_test.cc, its main renamed: the scarce-pool campaign
// of partial_control_pack.
#define main worker_test_main
#include "worker_test.cc"
#undef main

int main() {
	auto directory = std::filesystem::current_path() / ".scratch" / ("r5-pressure-" + std::to_string(::getpid()));
	std::filesystem::create_directories(directory); ::chmod(directory.c_str(), 0700);
	struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{directory};
	kronuz::journal::AdmissionLimits scarce{{1343, 100}, {1179, 3}, {0, 0}, 5, 3};
	{
		kronuz::journal::PosixIO io(directory); Worker worker(io, configuration(), scarce, limits(), compact);
		kronuz::journal::Identity identity{}; identity[0] = 'P'; worker.create(identity);
		while (!worker.ready()) { worker.run_one({100, 100}); }
		worker.try_submit(Start{}); worker.try_submit(Tick{100, 100});
		for (unsigned turn = 0; turn < 30 && worker.committed() < 1; ++turn) { worker.run_one({100, 100}); worker.take_actions(); }
	}
	kronuz::journal::PosixIO io(directory); Worker worker(io, configuration(), scarce, limits(), compact);
	worker.recover([](auto&) {}); while (!worker.ready()) { worker.run_one({100, 100}); }
	worker.try_submit(Start{}); worker.take_actions();
	int pressured = 0, past = 0;
	for (std::uint64_t now = 100; now <= 1000; now += 100) {  // a driver's wheel passes, its clock moving on
		const auto result = worker.try_submit(Tick{now, 100});
		const auto deadline = worker.next_deadline();
		pressured += result == SubmitResult::Pressure; past += result == SubmitResult::Pressure && deadline <= now;
		std::printf("Tick %llu: %s, next_deadline %llu\n", (unsigned long long)now, result == SubmitResult::Pressure ? "Pressure" : "admitted", (unsigned long long)deadline);
	}
	std::printf("%d Ticks refused with Pressure, %d of them leaving next_deadline at or before the clock\n", pressured, past);
	return pressured && past == pressured ? 0 : 1;  // 0: the Driver re-arms in the past on each one
}
