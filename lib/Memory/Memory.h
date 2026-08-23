#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

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

// Grows a vector's capacity without risking an abort(). std::vector's own growth
// throws bad_alloc, and a throwing allocation ends this build in terminate(), so the
// block is asked for through the nothrow path first and the caller decides what a
// refusal means. The probe is held while the vector still owns its old buffer, which
// is the moment reserve() needs both to coexist.
//
//   if (!reserveNoThrow(entries, entries.size() + 16)) { LOG_ERR("TAG", "OOM"); return false; }
//
template <typename T>
[[nodiscard]] bool reserveNoThrow(std::vector<T>& v, size_t capacity) {
  if (capacity <= v.capacity()) return true;
  auto probe = makeUniqueNoThrow<unsigned char[]>(capacity * sizeof(T));
  if (!probe) return false;
  probe.reset();
  v.reserve(capacity);
  return true;
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
