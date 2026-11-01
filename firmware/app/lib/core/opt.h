// Opt<T>: an optional value held by value ("absent" is a normal state, e.g. wall_ms
// unknown). Kept trivially copyable so slices stay plain data.
#pragma once

namespace cpa {

template <class T>
struct Opt {
  bool has = false;
  T value{};

  T value_or(const T& fallback) const { return has ? value : fallback; }
  bool operator==(const Opt& o) const { return has == o.has && (!has || value == o.value); }
  bool operator!=(const Opt& o) const { return !(*this == o); }
};

template <class T>
Opt<T> some(const T& v) {
  Opt<T> o;
  o.has = true;
  o.value = v;
  return o;
}

}  // namespace cpa
