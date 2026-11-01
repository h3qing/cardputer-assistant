// Result<T, E>: a value or an error, both held by value (no exceptions on device).
#pragma once

namespace cpa {

template <class T, class E>
struct Result {
  bool ok = false;
  T value{};
  E error{};

  static Result success(const T& v) {
    Result r;
    r.ok = true;
    r.value = v;
    return r;
  }

  static Result failure(const E& e) {
    Result r;
    r.error = e;
    return r;
  }
};

}  // namespace cpa
