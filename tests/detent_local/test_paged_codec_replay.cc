#include "dense_fixture.hh"
#include <filesystem>
#include <fstream>
#include <iostream>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *, std::size_t);
int main(int argc, char **argv) {
	dense_test::require(argc == 2);
	std::size_t count = 0;
	for (const auto &entry : std::filesystem::directory_iterator(argv[1])) {
		if (!entry.is_regular_file())
			continue;
		std::ifstream file(entry.path(), std::ios::binary);
		const std::vector<char> data{std::istreambuf_iterator<char>(file), {}};
		LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t *>(data.data()), data.size());
		++count;
	}
	dense_test::require(count > 0);
	std::cout << count << " dense decoder fuzz seeds replayed\n";
}
