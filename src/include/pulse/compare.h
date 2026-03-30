#ifndef COMPARE_H
#define COMPARE_H

#if !__has_include(<compare>)

// Required for using <=> operator, which is used in ETL classes.
namespace std {

struct strong_ordering {
    int value;

    constexpr explicit strong_ordering(int v):
        value(v)
    {}

    constexpr operator int()
    {
        return value;
    }

    static const strong_ordering less;
    static const strong_ordering equal;
    static const strong_ordering greater;
};

inline constexpr strong_ordering strong_ordering::less{-1};
inline constexpr strong_ordering strong_ordering::equal{0};
inline constexpr strong_ordering strong_ordering::greater{1};

} // namespace std

#endif // !__has_include(<compare>)

#endif /* COMPARE_H */
