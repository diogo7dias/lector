#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

// Nothrow versions of std::make_unique. Return nullptr on allocation failure
// instead of calling abort() (the default when exceptions are disabled on ESP32).
//
// Single object:
//   auto obj = makeUniqueNoThrow<PNG>();
//   if (!obj) { LOG_ERR("TAG", "OOM"); return false; }
//
// Array:
//   auto buf = makeUniqueNoThrow<uint8_t[]>(size);
//   if (!buf) { LOG_ERR("TAG", "OOM"); return false; }
//   buf[0] = 0xFF;
//   someApi(buf.get(), size);
//

// True when the largest free heap block can hold `bytes` plus allocator
// overhead. Guards the nothrow-new double-fault: libstdc++'s nothrow new wraps
// the THROWING operator new in a try/catch, and under a starved heap the
// internal bad_alloc's __cxa_allocate_exception ALSO fails and calls
// std::terminate()/abort() instead of the catch returning null (device-
// confirmed). Only enter new(nothrow) when this returns true. On host (no
// ESP32 heap API) always true; new(nothrow) there behaves correctly.
inline bool heapCanAllocate(size_t bytes) {
#if defined(ARDUINO_ARCH_ESP32)
  constexpr size_t ALLOC_HEADROOM = 512;
  return bytes + ALLOC_HEADROOM <= heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
#else
  (void)bytes;
  return true;
#endif
}

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  using Elem = std::remove_extent_t<T>;
  // See heapCanAllocate: on this toolchain new(nothrow)[] can terminate()
  // instead of returning null under extreme OOM (device-confirmed, v0.45 crash
  // 0x421965ff from a TextBlock arena slab). Only allocate when it will succeed.
  // Elem is trivial for every array use here, so a size check suffices.
  if (count > std::numeric_limits<size_t>::max() / sizeof(Elem)) return nullptr;  // overflow guard
  if (!heapCanAllocate(count * sizeof(Elem))) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) Elem[count]());
}

// Nothrow std::make_shared. std::make_shared uses throwing operator new for the
// combined control-block+object allocation; under -fno-exceptions a failure
// calls abort() rather than returning null, so any make_shared on a hot path is
// a latent crash on a starved heap. This constructs the object with
// new(std::nothrow) — the large allocation that actually fails on a fragmented
// heap — and returns nullptr on OOM so callers can drop the work gracefully.
//
//   auto block = makeSharedNoThrow<TextBlock>(args...);
//   if (!block) { LOG_ERR("TAG", "OOM"); return; }
//
// The object allocation is heap-pre-checked (see heapCanAllocate) so the
// nothrow-new double-fault cannot terminate() the device under extreme OOM.
//
// Residual: wrapping the object in shared_ptr still allocates a ~32-byte
// control block with throwing new. Once the (larger) object above has
// succeeded a block that small is effectively always available; on the
// vanishingly rare failure libstdc++ deletes the object first, then aborts —
// no leak. This is strictly safer than make_shared and makes the null-checks
// callers already write actually reachable.
template <typename T, typename... Args>
std::shared_ptr<T> makeSharedNoThrow(Args&&... args) {
  if (!heapCanAllocate(sizeof(T))) return nullptr;
  T* obj = new (std::nothrow) T(std::forward<Args>(args)...);
  if (!obj) return nullptr;
  return std::shared_ptr<T>(obj);
}

// Helper struct to call a cleanup function on exit from any scope.
// Use with a lambda to avoid unnecessary allocations from std::function/std::bind:
// Example:
//   auto jpeg = makeUniqueNoThrow<JPEGDEC>();
//   ScopedCleanup cleanup{[&jpeg]{ jpeg->close(); }};
//
template <typename F>
struct [[nodiscard]] ScopedCleanup final {
  const F fn;
  explicit ScopedCleanup(F f) : fn{std::move(f)} {}
  ScopedCleanup(const ScopedCleanup&) = delete;
  ScopedCleanup& operator=(const ScopedCleanup&) = delete;
  ScopedCleanup(ScopedCleanup&&) = delete;
  ScopedCleanup& operator=(ScopedCleanup&&) = delete;
  ~ScopedCleanup() { fn(); }
};

template <typename F>
ScopedCleanup(F) -> ScopedCleanup<F>;
