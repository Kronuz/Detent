// Lets a test observe every heap allocation. Include it once, in the test's
// only translation unit, after defining:
//
//   namespace detent::test {
//   struct AllocationProbe {
//   	static void allocate(std::size_t bytes);
//   	static void release(void *pointer) noexcept;	// optional
//   };
//   }
//
// Normally the whole replaceable set of global allocation functions is
// defined over malloc and free around the probe, and allocate() may throw
// std::bad_alloc to inject a failure. Under ThreadSanitizer on Linux the
// runtime owns those functions (clang links its static runtime whole, ahead
// of the test's objects), so the probe is called from the sanitizer's malloc
// hook instead: it observes and cannot throw. A test that injects failures
// is not built there (see DETENT_TSAN in tests/CMakeLists.txt). Apple's
// ThreadSanitizer is a dylib that the test's operators interpose, so it
// takes the normal path.
#pragma once

#include <cstddef>

#if !defined(__APPLE__)
#if defined(__SANITIZE_THREAD__)
#define DETENT_TEST_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define DETENT_TEST_TSAN 1
#endif
#endif
#endif

#ifdef DETENT_TEST_TSAN

namespace detent::test::detail {
template <class Probe = AllocationProbe> void observe_release(const volatile void *pointer) noexcept {
	if constexpr (requires { Probe::release(const_cast<void *>(pointer)); })
		Probe::release(const_cast<void *>(pointer));
}
} // namespace detent::test::detail

extern "C" void __sanitizer_malloc_hook(const volatile void *, std::size_t bytes) {
	detent::test::AllocationProbe::allocate(bytes);
}
extern "C" void __sanitizer_free_hook(const volatile void *pointer) {
	detent::test::detail::observe_release(pointer);
}

#else

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

#endif
