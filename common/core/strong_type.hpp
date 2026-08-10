#pragma once

#include <compare>
#include <functional>
#include <ostream>


/* A zero-overhead wrapper that gives the raw value type `T` a distinct
   identity via `Tag`, so e.g. a Price and a Volume (both int64_t underneath)
   can't be silently passed for one another. Supports the comparison and
   arithmetic operators needed to use it like `T` within its own type;
   crossing to another StrongType (or back to `T`) requires an explicit cast. */
template <typename T, typename Tag>
class StrongType {
public:
    using ValueType = T;

    constexpr StrongType() = default;
    constexpr explicit StrongType(T value) : value_(value) {}

    constexpr T value() const { return value_; }
    constexpr explicit operator T() const { return value_; }

    friend constexpr auto operator<=>(const StrongType&, const StrongType&) = default;
    friend constexpr bool operator==(const StrongType&, const StrongType&) = default;

    friend constexpr StrongType operator+(StrongType lhs, StrongType rhs) {
        return StrongType(lhs.value_ + rhs.value_);
    }
    friend constexpr StrongType operator-(StrongType lhs, StrongType rhs) {
        return StrongType(lhs.value_ - rhs.value_);
    }
    friend constexpr StrongType operator-(StrongType t) {
        return StrongType(-t.value_);
    }
    friend constexpr StrongType& operator+=(StrongType& lhs, StrongType rhs) {
        lhs.value_ += rhs.value_;
        return lhs;
    }
    friend constexpr StrongType& operator-=(StrongType& lhs, StrongType rhs) {
        lhs.value_ -= rhs.value_;
        return lhs;
    }

    friend std::ostream& operator<<(std::ostream& os, const StrongType& t) {
        return os << t.value_;
    }

private:
    T value_ {};
};

// Lets StrongType<T, Tag> drop into std::unordered_map/unordered_set keys.
template <typename T, typename Tag>
struct std::hash<StrongType<T, Tag>> {
    std::size_t operator()(const StrongType<T, Tag>& t) const noexcept {
        return std::hash<T>{}(t.value());
    }
};
