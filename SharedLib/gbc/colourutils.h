#pragma once

#include <cstdint>

// Swap bytes, then convert xBBBBBGG_GGGRRRRR to AAAAAAAA_BBBBBBBB_GGGGGGGG_RRRRRRRR
inline uint32_t REMAP_555_8888(unsigned int loByte, unsigned int hiByte) {
    uint32_t doubleByte = (hiByte << 8U) | loByte;
    return ((doubleByte & 0x001fU) << 3U) | ((doubleByte & 0x03e0U) << 6U) | ((doubleByte & 0x7c00U) << 9U) | 0xff000000U;
}
