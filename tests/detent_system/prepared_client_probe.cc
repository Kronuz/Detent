#include "detent/client.hh"
#include "detent_local/client_source.hh"
#include "names.hh"
#include "prepared_reader.hh"
#include <charconv>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace x = l::detail;
namespace {
template <class Integer> Integer integer(std::string_view text) {
	Integer value{};
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc{} || end != text.data() + text.size())
		throw std::invalid_argument("invalid probe integer");
	return value;
}
void prepare(char **argv) {
	const auto heap_bytes = integer<std::size_t>(argv[6]);
	auto heap = std::make_shared<d::StorageLedger>(heap_bytes);
	d::RetirementRuntime runtime(heap, 1);
	auto domain = runtime.domain(heap, 16384);
	{
		l::ViewLimits limits;
		limits.records = integer<std::uint32_t>(argv[3]);
		limits.bytes = integer<std::uint64_t>(argv[4]);
		limits.strings = integer<std::uint64_t>(argv[5]);
		limits.pages = limits.bytes / l::paged::page_header_bytes;
		limits.retirement_domain = &domain;
		limits.mapping_account = std::make_shared<d::StorageLedger>(integer<std::size_t>(argv[7]));
		const auto generation = l::recover_generation(argv[2], limits);
		const auto bytes = x::encode_reader_metadata(x::PreparedReader::describe(generation));
		const auto path =
		    std::filesystem::path(argv[2]) /
		    x::GenerationName(generation.descriptor().generation, l::detail::GenerationFile::ReaderMetadata)
		        .view();
		const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (fd < 0)
			throw std::runtime_error("cannot create probe metadata");
		std::size_t offset = 0;
		while (offset < bytes.size()) {
			const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0) {
				::close(fd);
				throw std::runtime_error("cannot write probe metadata");
			}
			offset += count;
		}
		if (::fsync(fd) != 0) {
			::close(fd);
			throw std::runtime_error("cannot sync probe metadata");
		}
		if (::close(fd) != 0)
			throw std::runtime_error("cannot close probe metadata");
		std::cout << "{\"prepared_records\":" << generation.view().size() << "}\n";
	}
	domain.close().wait();
}
void read(char **argv, std::uint64_t iterations) {
	const auto expected = integer<std::int64_t>(argv[4]);
	rusage before{}, after{};
	if (::getrusage(RUSAGE_SELF, &before) != 0)
		throw std::runtime_error("getrusage failed");
	const auto start = std::chrono::steady_clock::now();
	l::ClientSourceOptions source;
	source.directory = argv[2];
	source.limits = l::ViewLimits::prepared();
	source.prepared_publisher = ::getuid();
	detent::InitOptions options;
	options.read_source = l::make_local_read_source(std::move(source));
	detent::Client client(std::move(options));
	const auto attached = std::chrono::steady_clock::now();
	const auto value = client.get(argv[3]);
	if (!value || value.value() != expected)
		throw std::runtime_error("first live value differs");
	const auto stop = std::chrono::steady_clock::now();
	if (::getrusage(RUSAGE_SELF, &after) != 0)
		throw std::runtime_error("getrusage failed");
    double steady_ns = 0;
    if (iterations) {
        for (unsigned i = 0; i != 1000; ++i)
            if (client.get(argv[3]).value() != expected)
                throw std::runtime_error("warmup live value differs");
        const auto begin = std::chrono::steady_clock::now();
        for (std::uint64_t i = 0; i != iterations; ++i)
            if (client.get(argv[3]).value() != expected)
                throw std::runtime_error("steady live value differs");
        steady_ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - begin).count()) / double(iterations);
    }
	const auto cpu = [](const timeval &t) { return std::int64_t(t.tv_sec) * 1000000 + t.tv_usec; };
	std::cout << "{\"first_live_ns\":"
	          << std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()
	          << ",\"attach_ns\":"
	          << std::chrono::duration_cast<std::chrono::nanoseconds>(attached - start).count()
	          << ",\"lookup_ns\":"
	          << std::chrono::duration_cast<std::chrono::nanoseconds>(stop - attached).count()
	          << ",\"process_cpu_us\":"
	          << cpu(after.ru_utime) + cpu(after.ru_stime) - cpu(before.ru_utime) - cpu(before.ru_stime)
	          << ",\"minor_faults\":" << after.ru_minflt - before.ru_minflt
	          << ",\"major_faults\":" << after.ru_majflt - before.ru_majflt
	          << ",\"steady_mean_ns\":" << steady_ns
	          << ",\"records\":" << client.health().bool_knobs + client.health().int_knobs << "}\n";
}
} // namespace
int main(int argc, char **argv) {
	try {
		if (argc == 8 && std::string_view(argv[1]) == "--prepare")
			prepare(argv);
		else if ((argc == 5 || argc == 6) && std::string_view(argv[1]) == "--read")
            read(argv, argc == 6 ? integer<std::uint64_t>(argv[5]) : 0);
		else
			throw std::invalid_argument(
			    "use --prepare DIR RECORDS BYTES STRINGS HEAP MAPPINGS or --read DIR KNOB EXPECTED_INT [ITERATIONS]");
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
