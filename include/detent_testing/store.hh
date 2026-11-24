#pragma once
// Header-only C++ face of libdetent_testing's C face (D149): an application test publishes knob values
// into a test store and reads them through detent::Reader. Never link it into a shipped binary.
#include "detent_testing/generation_writer.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace detent::testing {
// One thread publishes at a time; readers in any thread or process see each publish once it returns.
class Store {
  public:
	// Opens the store directory, creating it (mode 0700) when absent. Throws std::runtime_error.
	explicit Store(const std::string &directory) : writer_(detent_testing_open(directory.c_str())) {
		if (!writer_)
			throw std::runtime_error(std::string("detent_testing_open: ") + detent_testing_open_error());
	}
	Store(Store &&other) noexcept : writer_(std::exchange(other.writer_, nullptr)) {}
	Store &operator=(Store &&other) noexcept {
		if (this != &other) {
			detent_testing_close(writer_);
			writer_ = std::exchange(other.writer_, nullptr);
		}
		return *this;
	}
	Store(const Store &) = delete;
	Store &operator=(const Store &) = delete;
	~Store() { detent_testing_close(writer_); }
	// Publishes the next revision holding exactly these knobs, given as the defaults-file JSON
	// ({"schema_version":1,"knobs":{"ns:name":{"default":1},...}}), and returns that revision.
	// Publishes an entry per namespace, as the agent writes the store (namespace-delivery D8).
	std::uint64_t publish(std::string_view json) {
		if (!writer_)
			throw std::logic_error("detent::testing::Store::publish: moved from");
		std::uint64_t revision = 0;
		if (detent_testing_publish(writer_, json.data(), json.size(), &revision) != 0)
			throw std::runtime_error(std::string("detent_testing_publish: ") + detent_testing_error(writer_));
		return revision;
	}

  private:
	detent_testing_writer *writer_;
};
} // namespace detent::testing
