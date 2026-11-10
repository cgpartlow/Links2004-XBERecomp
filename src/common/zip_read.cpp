// zip_read.h: the central directory, local headers, and an inflater in the manner of Mark Adler's puff (canonical
// Huffman codes decoded a bit at a time from per-length counts, which is plenty for a few megabytes)
#include "common/zip_read.h"

#include <cstring>

namespace links::zip {

namespace {

uint32_t Le16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t Le32(const uint8_t* p) { return Le16(p) | Le16(p + 2) << 16; }

constexpr uint32_t kEndSig = 0x06054B50;      // end of central directory
constexpr uint32_t kCentralSig = 0x02014B50;  // central directory entry
constexpr uint32_t kLocalSig = 0x04034B50;    // local file header

uint32_t Crc32(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---- inflate ----

constexpr int kMaxBits = 15;

struct Huffman {
    uint16_t count[kMaxBits + 1];  // codes of each length
    uint16_t symbol[288];          // symbols in canonical order
};

struct Bits {
    const uint8_t* in;
    size_t size;
    size_t at = 0;
    uint32_t buf = 0;
    int have = 0;
    bool bad = false;

    int Get(int n) {
        uint32_t v = buf;
        while (have < n) {
            if (at >= size) {
                bad = true;
                return 0;
            }
            v |= uint32_t(in[at++]) << have;
            have += 8;
        }
        buf = v >> n;
        have -= n;
        return int(v & ((1u << n) - 1));
    }
};

// 0 complete, > 0 incomplete (allowed for a single code), < 0 over-subscribed
int Build(Huffman& h, const uint8_t* lengths, int n) {
    std::memset(h.count, 0, sizeof(h.count));
    for (int s = 0; s < n; s++) h.count[lengths[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= kMaxBits; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    uint16_t offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; len++) offs[len + 1] = uint16_t(offs[len] + h.count[len]);
    for (int s = 0; s < n; s++)
        if (lengths[s]) h.symbol[offs[lengths[s]]++] = uint16_t(s);
    return left;
}

int Decode(Bits& b, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; len++) {
        code |= b.Get(1);
        if (b.bad) return -1;
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint16_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(Bits& b, std::vector<uint8_t>& out, const Huffman& lens, const Huffman& dists) {
    for (;;) {
        int sym = Decode(b, lens);
        if (sym < 0) return false;
        if (sym < 256) {
            out.push_back(uint8_t(sym));
            continue;
        }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;
        size_t len = kLenBase[sym] + size_t(b.Get(kLenExtra[sym]));
        int ds = Decode(b, dists);
        if (ds < 0 || ds >= 30) return false;
        size_t dist = kDistBase[ds] + size_t(b.Get(kDistExtra[ds]));
        if (b.bad || dist > out.size()) return false;
        size_t from = out.size() - dist;
        for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
    }
}

bool Stored(Bits& b, std::vector<uint8_t>& out) {
    b.buf = 0;
    b.have = 0;  // to the byte boundary
    if (b.at + 4 > b.size) return false;
    uint32_t len = Le16(b.in + b.at), nlen = Le16(b.in + b.at + 2);
    b.at += 4;
    if (len != (~nlen & 0xFFFF) || b.at + len > b.size) return false;
    out.insert(out.end(), b.in + b.at, b.in + b.at + len);
    b.at += len;
    return true;
}

bool Fixed(Bits& b, std::vector<uint8_t>& out) {
    static Huffman lens, dists;
    static bool made = false;
    if (!made) {
        uint8_t l[288];
        int s = 0;
        for (; s < 144; s++) l[s] = 8;
        for (; s < 256; s++) l[s] = 9;
        for (; s < 280; s++) l[s] = 7;
        for (; s < 288; s++) l[s] = 8;
        Build(lens, l, 288);
        for (s = 0; s < 30; s++) l[s] = 5;
        Build(dists, l, 30);
        made = true;
    }
    return Codes(b, out, lens, dists);
}

bool Dynamic(Bits& b, std::vector<uint8_t>& out) {
    static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = b.Get(5) + 257, ndist = b.Get(5) + 1, ncode = b.Get(4) + 4;
    if (b.bad || nlen > 286 || ndist > 30) return false;
    uint8_t lengths[320] = {};
    for (int i = 0; i < ncode; i++) lengths[kOrder[i]] = uint8_t(b.Get(3));
    Huffman lencode, distcode;
    if (Build(lencode, lengths, 19) != 0) return false;  // the code length code must be complete
    int at = 0;
    while (at < nlen + ndist) {
        int sym = Decode(b, lencode);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[at++] = uint8_t(sym);
            continue;
        }
        uint8_t len = 0;
        int repeat;
        if (sym == 16) {
            if (at == 0) return false;
            len = lengths[at - 1];
            repeat = 3 + b.Get(2);
        } else if (sym == 17) {
            repeat = 3 + b.Get(3);
        } else {
            repeat = 11 + b.Get(7);
        }
        if (b.bad || at + repeat > nlen + ndist) return false;
        while (repeat--) lengths[at++] = len;
    }
    if (lengths[256] == 0) return false;  // no end of block code
    // an incomplete code is allowed only when it has a single symbol
    int err = Build(lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
    err = Build(distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return false;
    return Codes(b, out, lencode, distcode);
}

}  // namespace

bool Inflate(const uint8_t* in, size_t size, std::vector<uint8_t>& out) {
    Bits b{in, size};
    for (;;) {
        int last = b.Get(1), type = b.Get(2);
        if (b.bad) return false;
        bool ok = type == 0 ? Stored(b, out) : type == 1 ? Fixed(b, out) : type == 2 ? Dynamic(b, out) : false;
        if (!ok) return false;
        if (last) return true;
    }
}

bool List(const std::vector<uint8_t>& a, std::vector<Entry>& out) {
    out.clear();
    if (a.size() < 22) return false;
    // the end record is in the last 64 KB and 22 bytes (its comment is at most 65535)
    size_t end = a.size() - 22, stop = a.size() > 65557 ? a.size() - 65557 : 0;
    for (;; end--) {
        if (Le32(&a[end]) == kEndSig) break;
        if (end == stop) return false;
    }
    uint32_t count = Le16(&a[end + 10]), dir = Le32(&a[end + 16]);
    size_t at = dir;
    for (uint32_t i = 0; i < count; i++) {
        if (at + 46 > a.size() || Le32(&a[at]) != kCentralSig) return false;
        const uint8_t* c = &a[at];
        uint32_t name_len = Le16(c + 28), extra = Le16(c + 30), comment = Le16(c + 32);
        if (at + 46 + name_len > a.size()) return false;
        Entry e;
        e.method = Le16(c + 10);
        e.crc = Le32(c + 16);
        e.packed = Le32(c + 20);
        e.size = Le32(c + 24);
        e.local = Le32(c + 42);
        e.name.assign(reinterpret_cast<const char*>(c + 46), name_len);
        out.push_back(std::move(e));
        at += 46 + size_t(name_len) + extra + comment;
    }
    return true;
}

bool Extract(const std::vector<uint8_t>& a, const Entry& e, std::vector<uint8_t>& out) {
    out.clear();
    if (size_t(e.local) + 30 > a.size() || Le32(&a[e.local]) != kLocalSig) return false;
    const uint8_t* l = &a[e.local];
    if (Le16(l + 6) & 1) return false;  // encrypted
    size_t data = size_t(e.local) + 30 + Le16(l + 26) + Le16(l + 28);
    if (data + e.packed > a.size()) return false;
    if (e.method == 0) {
        if (e.packed != e.size) return false;
        out.assign(a.begin() + long(data), a.begin() + long(data + e.size));
    } else if (e.method == 8) {
        out.reserve(e.size);
        if (!Inflate(&a[data], e.packed, out)) return false;
    } else {
        return false;
    }
    return out.size() == e.size && Crc32(out.data(), out.size()) == e.crc;
}

}  // namespace links::zip
