#pragma once
// Reading zip archives held in memory: the game's GUI resource packs (GuiPackages\*.ruirr are zips) and the upscaler's
// release download. Stored and deflated entries; no encryption, no zip64.
#include <cstdint>
#include <string>
#include <vector>

namespace links::zip {

struct Entry {
    std::string name;  // as the archive has it, '/' separated
    uint32_t method = 0;  // 0 stored, 8 deflated
    uint32_t crc = 0;     // of the bytes, from the central directory
    uint32_t packed = 0, size = 0;
    uint32_t local = 0;   // its local header's offset
};

// the archive's entries from its central directory; false when it is no zip this reads
bool List(const std::vector<uint8_t>& archive, std::vector<Entry>& out);

// one entry's bytes, their CRC checked; false when they are damaged or packed some other way
bool Extract(const std::vector<uint8_t>& archive, const Entry& e, std::vector<uint8_t>& out);

// raw deflate data (RFC 1951) into out, which it appends to; false on bad data
bool Inflate(const uint8_t* in, size_t size, std::vector<uint8_t>& out);

}  // namespace links::zip
