#ifndef CORECOMMON_SRC_BITFLAGS_HPP_
#define CORECOMMON_SRC_BITFLAGS_HPP_

//originated from https://softwareengineering.stackexchange.com/a/338472
#include <type_traits>
#include <bitset>

template<typename Enum, bool IsEnum = std::is_enum<Enum>::value>
class bitflags;

template<typename Enum>
class bitflags<Enum, true> {
 public:
	static const unsigned number_of_bits = std::numeric_limits<typename std::underlying_type<Enum>::type>::digits;

	constexpr bitflags() = default;
	constexpr bitflags(Enum value) : bits(1 << static_cast<std::size_t>(value)) {}
	constexpr bitflags(const bitflags& other) : bits(other.bits) {}

	constexpr bitflags operator|(Enum value) const { bitflags result = *this; result.bits |= 1 << static_cast<std::size_t>(value); return result; }
	constexpr bitflags operator&(Enum value) const { bitflags result = *this; result.bits &= 1 << static_cast<std::size_t>(value); return result; }
	constexpr bitflags operator^(Enum value) const { bitflags result = *this; result.bits ^= 1 << static_cast<std::size_t>(value); return result; }
	constexpr bitflags operator~() const { bitflags result = *this; result.bits.flip(); return result; }

	constexpr bitflags& operator|=(Enum value) { bits |= 1 << static_cast<std::size_t>(value); return *this; }
	constexpr bitflags& operator&=(Enum value) { bits &= 1 << static_cast<std::size_t>(value); return *this; }
	constexpr bitflags& operator^=(Enum value) { bits ^= 1 << static_cast<std::size_t>(value); return *this; }

	constexpr bool any() const { return bits.any(); }
	constexpr bool all() const { return bits.all(); }
	constexpr bool none() const { return bits.none(); }
	constexpr operator bool() const { return any(); }

	constexpr bool test(Enum value) const { return bits.test(1 << static_cast<std::size_t>(value)); }
	constexpr void set(Enum value) { bits.set(1 << static_cast<std::size_t>(value)); }
	constexpr void unset(Enum value) { bits.reset(1 << static_cast<std::size_t>(value)); }

 private:
	std::bitset<number_of_bits> bits;
};

template<typename Enum>
constexpr typename std::enable_if<std::is_enum<Enum>::value, bitflags<Enum>>::type operator|(Enum left, Enum right)
{
	return bitflags<Enum>(left) | right;
}
template<typename Enum>
constexpr typename std::enable_if<std::is_enum<Enum>::value, bitflags<Enum>>::type operator&(Enum left, Enum right)
{
	return bitflags<Enum>(left) & right;
}
template<typename Enum>
constexpr typename std::enable_if_t<std::is_enum<Enum>::value, bitflags<Enum>>::type operator^(Enum left, Enum right)
{
	return bitflags<Enum>(left) ^ right;
}

#endif //CORECOMMON_SRC_BITFLAGS_HPP_
