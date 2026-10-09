#include "directory_internal.hh"
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace l = detent::local;
namespace {
unsigned calls = 0, fail_at = 0, replace_at = 0;
std::filesystem::path replacement;
void check(bool value) {
	if (!value)
		throw std::runtime_error("directory provisioning invariant");
}
void synchronize(int descriptor) {
	++calls;
	if (calls == fail_at)
		throw std::runtime_error("injected directory barrier failure");
	check(::fsync(descriptor) == 0);
	if (calls == replace_at) {
		std::filesystem::rename(replacement, replacement.string() + ".displaced");
		std::filesystem::create_directory(replacement);
	}
}
unsigned mode(const std::filesystem::path &path) {
	struct stat info{};
	check(::stat(path.c_str(), &info) == 0);
	return info.st_mode & 0777;
}
template <class Operation> void rejects(Operation operation) {
	bool rejected = false;
	try {
		operation();
	} catch (const std::exception &) {
		rejected = true;
	}
	check(rejected);
}
} // namespace
int main() {
	try {
		const auto root = std::filesystem::current_path() / ".scratch" /
						  ("directory-provisioning-" + std::to_string(::getpid()));
		std::filesystem::create_directories(root);
		std::filesystem::permissions(root, std::filesystem::perms::owner_all);
		const auto nested = root / "reference" / "a" / "b";
		auto reference = l::detail::DirectoryProvisioning::run(nested, 0700, synchronize);
		reference.verify_name();
		check(mode(nested) == 0700 && mode(nested.parent_path()) == 0700);
		const auto barriers = calls;
		check(barriers >= 6);
		for (unsigned index = 1; index <= barriers; ++index) {
			calls = 0;
			fail_at = index;
			const auto candidate = root / ("failure-" + std::to_string(index)) / "a" / "b";
			rejects([&] { l::detail::DirectoryProvisioning::run(candidate, 0700, synchronize); });
			check(calls == index);
			fail_at = 0;
			auto retried = l::provision_directory(candidate, 0700);
			retried.verify_name();
			check(mode(candidate) == 0700);
		}
		const auto existing = root / "existing";
		std::filesystem::create_directory(existing);
		std::filesystem::permissions(existing, std::filesystem::perms::owner_all |
												   std::filesystem::perms::group_read |
												   std::filesystem::perms::group_exec);
		auto retained = l::provision_directory(existing, 0755);
		check(mode(existing) == 0750);
		std::filesystem::create_directory_symlink(existing, root / "link");
		rejects([&] { l::provision_directory(root / "link", 0700); });
		rejects([&] { l::provision_directory((root / "link").string() + "/", 0700); });
		std::filesystem::rename(existing, root / "displaced");
		std::filesystem::create_directory(existing);
		rejects([&] { retained.verify_name(); });
		calls = 0;
		replacement = root / "replaced" / "a" / "b";
		replace_at = barriers;
		rejects([&] { l::detail::DirectoryProvisioning::run(replacement, 0700, synchronize); });
		check(calls == barriers);
		replace_at = 0;
		std::filesystem::remove_all(root);
		std::cout << "nested provisioning, " << barriers
				  << " barrier failure/retry schedules, "
					 "existing modes, final symlink and replacement rejection passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
