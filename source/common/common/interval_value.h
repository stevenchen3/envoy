#pragma once

#include <algorithm>

namespace Envoy {

// Template helper type that represents a closed interval with the given minimum and maximum values.
template <typename T, T MinValue, T MaxValue> struct Interval {
  static_assert(MinValue <= MaxValue, "min must be <= max");
  static constexpr T min_value = MinValue;
  static constexpr T max_value = MaxValue;
};

// Utility type that represents a value of type T in the given interval.
template <typename T, typename Interval> class ClosedIntervalValue {
public:
  static constexpr ClosedIntervalValue min() { return ClosedIntervalValue(Interval::min_value); }
  static constexpr ClosedIntervalValue max() { return ClosedIntervalValue(Interval::max_value); }

  constexpr explicit ClosedIntervalValue(T value)
      : value_(std::max<T>(Interval::min_value, std::min<T>(Interval::max_value, value))) {}

  T value() const { return value_; }

private:
  T value_;
};

// C++17 doesn't allow templating on floating point values, otherwise that's
// what we should do here instead of relying on a int => float implicit
// conversion. TODO(akonradi): when Envoy is using C++20, switch these template
// parameters to floats.

// Floating point value in the range [0, 1].
using UnitFloat = ClosedIntervalValue<float, Interval<int, 0, 1>>;

} // namespace Envoy
