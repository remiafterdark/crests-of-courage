

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace seal {

inline constexpr uint32_t kMagic = 0x31535043u;
inline constexpr size_t kHeader = 16;
inline constexpr size_t kTag = 16;
inline constexpr size_t kOverhead = kHeader + kTag;
inline constexpr uint32_t kFromHost = 1;
inline constexpr uint32_t kFromJoiner = 2;

inline uint32_t load32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

inline void store32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline void store64(uint8_t* p, uint64_t v) {
    store32(p, static_cast<uint32_t>(v));
    store32(p + 4, static_cast<uint32_t>(v >> 32));
}

inline uint64_t load64(const uint8_t* p) {
    return static_cast<uint64_t>(load32(p)) | static_cast<uint64_t>(load32(p + 4)) << 32;
}

inline uint32_t rotl(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

inline void chacha_block(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
    uint8_t out[64]) {
    uint32_t s[16] = {0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u};
    for (int i = 0; i < 8; ++i) s[4 + i] = load32(key + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; ++i) s[13 + i] = load32(nonce + 4 * i);
    uint32_t x[16];
    std::memcpy(x, s, sizeof(x));
    auto quarter = [&x](int a, int b, int c, int d) {
        x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 16);
        x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 12);
        x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 8);
        x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 7);
    };
    for (int round = 0; round < 10; ++round) {
        quarter(0, 4, 8, 12);
        quarter(1, 5, 9, 13);
        quarter(2, 6, 10, 14);
        quarter(3, 7, 11, 15);
        quarter(0, 5, 10, 15);
        quarter(1, 6, 11, 12);
        quarter(2, 7, 8, 13);
        quarter(3, 4, 9, 14);
    }
    for (int i = 0; i < 16; ++i) store32(out + 4 * i, x[i] + s[i]);
}

inline void chacha_xor(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
    const uint8_t* in, uint8_t* out, size_t size) {
    uint8_t block[64];
    for (size_t at = 0; at < size; at += 64, ++counter) {
        chacha_block(key, counter, nonce, block);
        const size_t n = size - at < 64 ? size - at : 64;
        for (size_t i = 0; i < n; ++i) out[at + i] = static_cast<uint8_t>(in[at + i] ^ block[i]);
    }
}

struct Poly {
    uint32_t r[5];
    uint32_t h[5] = {};
    uint32_t pad[4];

    explicit Poly(const uint8_t key[32]) {
        r[0] = load32(key) & 0x3ffffff;
        r[1] = (load32(key + 3) >> 2) & 0x3ffff03;
        r[2] = (load32(key + 6) >> 4) & 0x3ffc0ff;
        r[3] = (load32(key + 9) >> 6) & 0x3f03fff;
        r[4] = (load32(key + 12) >> 8) & 0x00fffff;
        for (int i = 0; i < 4; ++i) pad[i] = load32(key + 16 + 4 * i);
    }

    void block(const uint8_t m[16], uint32_t high) {
        const uint32_t s1 = r[1] * 5, s2 = r[2] * 5, s3 = r[3] * 5, s4 = r[4] * 5;
        h[0] += load32(m) & 0x3ffffff;
        h[1] += (load32(m + 3) >> 2) & 0x3ffffff;
        h[2] += (load32(m + 6) >> 4) & 0x3ffffff;
        h[3] += (load32(m + 9) >> 6) & 0x3ffffff;
        h[4] += (load32(m + 12) >> 8) | high;
        const uint64_t d0 = static_cast<uint64_t>(h[0]) * r[0] + static_cast<uint64_t>(h[1]) * s4 +
            static_cast<uint64_t>(h[2]) * s3 + static_cast<uint64_t>(h[3]) * s2 +
            static_cast<uint64_t>(h[4]) * s1;
        uint64_t d1 = static_cast<uint64_t>(h[0]) * r[1] + static_cast<uint64_t>(h[1]) * r[0] +
            static_cast<uint64_t>(h[2]) * s4 + static_cast<uint64_t>(h[3]) * s3 +
            static_cast<uint64_t>(h[4]) * s2;
        uint64_t d2 = static_cast<uint64_t>(h[0]) * r[2] + static_cast<uint64_t>(h[1]) * r[1] +
            static_cast<uint64_t>(h[2]) * r[0] + static_cast<uint64_t>(h[3]) * s4 +
            static_cast<uint64_t>(h[4]) * s3;
        uint64_t d3 = static_cast<uint64_t>(h[0]) * r[3] + static_cast<uint64_t>(h[1]) * r[2] +
            static_cast<uint64_t>(h[2]) * r[1] + static_cast<uint64_t>(h[3]) * r[0] +
            static_cast<uint64_t>(h[4]) * s4;
        uint64_t d4 = static_cast<uint64_t>(h[0]) * r[4] + static_cast<uint64_t>(h[1]) * r[3] +
            static_cast<uint64_t>(h[2]) * r[2] + static_cast<uint64_t>(h[3]) * r[1] +
            static_cast<uint64_t>(h[4]) * r[0];
        uint64_t c = d0 >> 26;
        h[0] = static_cast<uint32_t>(d0) & 0x3ffffff;
        d1 += c; c = d1 >> 26; h[1] = static_cast<uint32_t>(d1) & 0x3ffffff;
        d2 += c; c = d2 >> 26; h[2] = static_cast<uint32_t>(d2) & 0x3ffffff;
        d3 += c; c = d3 >> 26; h[3] = static_cast<uint32_t>(d3) & 0x3ffffff;
        d4 += c; c = d4 >> 26; h[4] = static_cast<uint32_t>(d4) & 0x3ffffff;
        h[0] += static_cast<uint32_t>(c) * 5;
        h[1] += h[0] >> 26;
        h[0] &= 0x3ffffff;
    }

    void padded(const uint8_t* data, size_t size) {
        uint8_t last[16];
        for (size_t at = 0; at < size; at += 16) {
            if (size - at >= 16) {
                block(data + at, 1u << 24);
            } else {
                std::memset(last, 0, sizeof(last));
                std::memcpy(last, data + at, size - at);
                block(last, 1u << 24);
            }
        }
    }

    void finish(uint8_t tag[16]) {
        uint32_t c = h[1] >> 26; h[1] &= 0x3ffffff;
        h[2] += c; c = h[2] >> 26; h[2] &= 0x3ffffff;
        h[3] += c; c = h[3] >> 26; h[3] &= 0x3ffffff;
        h[4] += c; c = h[4] >> 26; h[4] &= 0x3ffffff;
        h[0] += c * 5; c = h[0] >> 26; h[0] &= 0x3ffffff;
        h[1] += c;
        uint32_t g[5];
        c = 5;
        for (int i = 0; i < 5; ++i) {
            g[i] = h[i] + c;
            c = g[i] >> 26;
            g[i] &= 0x3ffffff;
        }

        const uint32_t mask = 0u - c;
        for (int i = 0; i < 5; ++i) h[i] = (h[i] & ~mask) | (g[i] & mask);
        const uint32_t w0 = h[0] | (h[1] << 26);
        const uint32_t w1 = (h[1] >> 6) | (h[2] << 20);
        const uint32_t w2 = (h[2] >> 12) | (h[3] << 14);
        const uint32_t w3 = (h[3] >> 18) | (h[4] << 8);
        uint64_t f = static_cast<uint64_t>(w0) + pad[0];
        store32(tag, static_cast<uint32_t>(f));
        f = static_cast<uint64_t>(w1) + pad[1] + (f >> 32);
        store32(tag + 4, static_cast<uint32_t>(f));
        f = static_cast<uint64_t>(w2) + pad[2] + (f >> 32);
        store32(tag + 8, static_cast<uint32_t>(f));
        f = static_cast<uint64_t>(w3) + pad[3] + (f >> 32);
        store32(tag + 12, static_cast<uint32_t>(f));
    }
};

inline void tag_of(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad,
    size_t aadSize, const uint8_t* cipher, size_t size, uint8_t tag[16]) {
    uint8_t polyKey[64];
    chacha_block(key, 0, nonce, polyKey);
    Poly poly(polyKey);
    poly.padded(aad, aadSize);
    poly.padded(cipher, size);
    uint8_t lengths[16];
    store64(lengths, aadSize);
    store64(lengths + 8, size);
    poly.block(lengths, 1u << 24);
    poly.finish(tag);
}

inline void nonce_for(uint32_t sender, uint64_t counter, uint8_t nonce[12]) {
    store32(nonce, sender);
    store64(nonce + 4, counter);
}

inline void wrap(const uint8_t key[32], uint32_t keyId, uint32_t sender, uint64_t counter,
    const uint8_t* data, size_t size, uint8_t* out) {
    store32(out, kMagic);
    store32(out + 4, keyId);
    store64(out + 8, counter);
    uint8_t nonce[12];
    nonce_for(sender, counter, nonce);
    chacha_xor(key, 1, nonce, data, out + kHeader, size);
    tag_of(key, nonce, out, kHeader, out + kHeader, size, out + kHeader + size);
}

inline bool looks_sealed(const uint8_t* packet, size_t size) {
    return size >= kOverhead && load32(packet) == kMagic;
}

inline uint32_t key_id_of(const uint8_t* packet) {
    return load32(packet + 4);
}

inline uint64_t counter_of(const uint8_t* packet) {
    return load64(packet + 8);
}

inline bool unwrap(const uint8_t key[32], uint32_t sender, const uint8_t* packet, size_t size,
    uint8_t* out) {
    if (!looks_sealed(packet, size)) return false;
    const size_t body = size - kOverhead;
    uint8_t nonce[12];
    nonce_for(sender, counter_of(packet), nonce);
    uint8_t tag[16];
    tag_of(key, nonce, packet, kHeader, packet + kHeader, body, tag);
    uint8_t diff = 0;
    for (int i = 0; i < 16; ++i) diff |= static_cast<uint8_t>(tag[i] ^ packet[kHeader + body + i]);
    if (diff != 0) return false;
    chacha_xor(key, 1, nonce, packet + kHeader, out, body);
    return true;
}

struct Window {
    uint64_t newest = 0;
    uint64_t seen = 0;

    bool take(uint64_t counter) {
        if (counter == 0) return false;
        if (counter > newest) {
            const uint64_t ahead = counter - newest;
            seen = ahead >= 64 ? 0 : seen << ahead;
            seen |= 1;
            newest = counter;
            return true;
        }
        const uint64_t behind = newest - counter;
        if (behind >= 64) return false;
        const uint64_t bit = 1ull << behind;
        if ((seen & bit) != 0) return false;
        seen |= bit;
        return true;
    }
};

}
