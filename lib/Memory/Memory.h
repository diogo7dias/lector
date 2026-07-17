#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

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

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  using Elem = std::remove_extent_t<T>;
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
// Residual: wrapping the object in shared_ptr still allocates a ~32-byte
// control block with throwing new. Once the (larger) object above has
// succeeded a block that small is effectively always available; on the
// vanishingly rare failure libstdc++ deletes the object first, then aborts —
// no leak. This is strictly safer than make_shared and makes the null-checks
// callers already write actually reachable.
template <typename T, typename... Args>
std::shared_ptr<T> makeSharedNoThrow(Args&&... args) {
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
