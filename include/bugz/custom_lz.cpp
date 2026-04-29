//
// Created by red_eye on 4/12/26.
//
#include "custom_lz.hpp"

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <span>

#define LOBYTE(x)  (*reinterpret_cast<uint8_t*>(&(x)))
#define HIBYTE(x)  (*(reinterpret_cast<uint8_t*>(&(x)) + 1))
#define LOWORD(x)  (*reinterpret_cast<uint16_t*>(&(x)))

namespace custom_lz {
    LzResult lz_decompress(const u8 *src, u8 *dst) {
        const u8 *src_start = src;
        u8       *dst_start = dst;

        unsigned int v2;
        int v3;
        int v4;
        char v5;
        i16 v6;
        int v7;
        i16 v8;
        int v10;
        const u8 *v11;
        int v12;
        u16 v13;
        i16 v14;
        char v15;
        u16 v16;
        u8 *v17;
        int v18;
        char v19;
        i16 v20;
        u8 result;
        char v22;
        char v23;
        u16 v24;
        int v25;
        char v26;
        u16 v27[128];

        v2 = 0x7Fu >> (*src & 7);
        v22 = 1;
        v3 = 19;
        v23 = *src & 7;
        if (v2 < 0x1Fu)
            LOWORD(v3) = v2 >> 1;
        v26 = 7 - v23;
        v4 = 0;
        v5 = (*src >> 3) & 3;
        do {
            v6 = v4;
            if (v4 > v3)
                v6 = v3 + ((v4 - v3) << v5);
            v7 = v4++;
            v27[v7] = v6 + 2;
            v8 = v2;
            v2 += 0xFFFF;
        } while (v8);
        v10 = src[3] + (src[2] << 8) + (src[1] << 16);
        v11 = src + 4;
        v25 = v10;
        v12 = 127 >> v23;
        do {
            LOBYTE(v13) = *v11;
            HIBYTE(v13) = 1;
            ++v11;
            v24 = v13;
            if (v13) {
                while (1) {
                    v14 = v24 & 1;
                    v24 >>= 1;
                    if (!v24)
                        break;
                    if (v14) {
                        v15 = *v11++;
                        *dst++ = v15;
                    } else {
                        v16 = (*v11 << 8) + v11[1];
                        v11 += 2;
                        v17 = &dst[-(v16 >> v26)];
                        v18 = v12;
                        LOWORD(v18) = v27[v12 & v16];
                        do {
                            v19 = *v17++;
                            *dst = v19;
                            v20 = v18;
                            ++dst;
                            v18 += 0xFFFF;
                        } while (v20);
                        v10 = v25;
                    }
                    if (!v10) {
                        v22 = 0;
                        break;
                    }
                    v25 = --v10;
                }
            }
            result = v22;
        } while (v22);
        return { result, static_cast<u64>(v11 - src_start), static_cast<u64>(dst - dst_start) };
    }
}
