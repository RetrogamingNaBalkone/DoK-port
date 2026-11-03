#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace dok::original {
constexpr std::int16_t signed16(std::uint16_t n) {
    return n<=32767 ? static_cast<std::int16_t>(n) : static_cast<std::int16_t>(std::int32_t(n)-65536);
}
constexpr std::int32_t signed32(std::uint32_t n) {
    return n<=0x7fffffffu ? static_cast<std::int32_t>(n) : static_cast<std::int32_t>(std::int64_t(n)-4294967296ll);
}
constexpr std::int16_t negate16(std::int16_t n){return signed16(static_cast<std::uint16_t>(0u-static_cast<std::uint16_t>(n)));}
constexpr std::int32_t add32(std::int32_t a,std::int32_t b){return signed32(std::uint32_t(a)+std::uint32_t(b));}
constexpr std::int32_t sub32(std::int32_t a,std::int32_t b){return signed32(std::uint32_t(a)-std::uint32_t(b));}
constexpr std::int32_t multiply32(std::int32_t a,std::int32_t b){return signed32(std::uint32_t(a)*std::uint32_t(b));}
constexpr std::int32_t sar32(std::int32_t n,unsigned count){
    count&=31;if(!count)return n;
    auto bits=std::uint32_t(n)>>count;
    if(n<0)bits|=(~std::uint32_t(0))<<(32-count);
    return signed32(bits);
}
inline std::int32_t divide32(std::int32_t a,std::int32_t b){
    if(!b||(a==std::numeric_limits<std::int32_t>::min()&&b==-1))throw std::domain_error("Original IDIV fault");
    return a/b;
}
}
