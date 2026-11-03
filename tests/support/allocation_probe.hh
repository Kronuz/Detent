// Replaces every global allocation function with a malloc-backed one that
// first calls the test's probe. Include it once, in the test's only
// translation unit, after defining:
//
//   namespace detent::test {
//   struct AllocationProbe {
//   	static void allocate(std::size_t bytes);	// may throw std::bad_alloc
//   	static void release(void *pointer) noexcept;	// optional
//   };
//   }
//
// The whole replaceable set is defined, so ThreadSanitizer's runtime (which
// defines the same set in one object) is never linked beside it. The probe
// observes the calls; it never changes what a sanitizer sees of the heap.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

namespace detent::test::detail {

inline void *allocate(std::size_t bytes) {
	AllocationProbe::allocate(bytes);
	if (auto *pointer = std::malloc(bytes ? bytes : 1))
		return pointer;
	throw std::bad_alloc();
}

inline void *allocate_aligned(std::size_t bytes, std::align_val_t alignment) {
	AllocationProbe::allocate(bytes);
	const auto align = static_cast<std::size_t>(alignment);
	const auto rounded = (bytes ? bytes + align - 1 : align) / align * align;
	if (auto *pointer = std::aligned_alloc(align, rounded))
		return pointer;
	throw std::bad_alloc();
}

template <class Probe = AllocationProbe> void release(void *pointer) noexcept {
	if constexpr (requires { Probe::release(pointer); })
		Probe::release(pointer);
	std::free(pointer);
}

template <class Allocate> void *try_allocate(Allocate allocate) noexcept {
	try {
		return allocate();
	} catch (...) {
		return nullptr;
	}
}

} // namespace detent::test::detail

void *operator new(std::size_t bytes) { return detent::test::detail::allocate(bytes); }
void *operator new[](std::size_t bytes) { return detent::test::detail::allocate(bytes); }
void *operator new(std::size_t bytes, const std::nothrow_t &) noexcept {
	return detent::test::detail::try_allocate([&] { return detent::test::detail::allocate(bytes); });
}
void *operator new[](std::size_t bytes, const std::nothrow_t &) noexcept {
	return detent::test::detail::try_allocate([&] { return detent::test::detail::allocate(bytes); });
}
void *operator new(std::size_t bytes, std::align_val_t alignment) {
	return detent::test::detail::allocate_aligned(bytes, alignment);
}
void *operator new[](std::size_t bytes, std::align_val_t alignment) {
	return detent::test::detail::allocate_aligned(bytes, alignment);
}
void *operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t &) noexcept {
	return detent::test::detail::try_allocate(
		[&] { return detent::test::detail::allocate_aligned(bytes, alignment); });
}
void *operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t &) noexcept {
	return detent::test::detail::try_allocate(
		[&] { return detent::test::detail::allocate_aligned(bytes, alignment); });
}

void operator delete(void *pointer) noexcept { detent::test::detail::release(pointer); }
void operator delete[](void *pointer) noexcept { detent::test::detail::release(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { detent::test::detail::release(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { detent::test::detail::release(pointer); }
void operator delete(void *pointer, const std::nothrow_t &) noexcept {
	detent::test::detail::release(pointer);
}
void operator delete[](void *pointer, const std::nothrow_t &) noexcept {
	detent::test::detail::release(pointer);
}
void operator delete(void *pointer, std::align_val_t) noexcept { detent::test::detail::release(pointer); }
void operator delete[](void *pointer, std::align_val_t) noexcept {
	detent::test::detail::release(pointer);
}
void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
	detent::test::detail::release(pointer);
}
void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
	detent::test::detail::release(pointer);
}
void operator delete(void *pointer, std::align_val_t, const std::nothrow_t &) noexcept {
	detent::test::detail::release(pointer);
}
void operator delete[](void *pointer, std::align_val_t, const std::nothrow_t &) noexcept {
	detent::test::detail::release(pointer);
}
