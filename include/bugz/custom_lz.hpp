//
// Created by red_eye on 4/12/26.
//

#pragma once

#include <span>
#include <vector>

#include "redscore/int_def.h"

namespace custom_lz {
    struct LzResult {
        u8 status; // original return value
        u64 bytes_read; // bytes consumed from src
        u64 bytes_written; // bytes written to dst
    };

    LzResult lz_decompress(const u8 *src, u8 *dst);
}
