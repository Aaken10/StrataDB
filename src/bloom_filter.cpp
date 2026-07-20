#include "stratadb/bloom_filter.h"
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace stratadb {

BloomFilter::BloomFilter()
    : bits_per_key_(10), num_hashes_(1), key_count_(0) {}

BloomFilter::BloomFilter(size_t bits_per_key)
    : bits_per_key_(bits_per_key), key_count_(0) {
    num_hashes_ = std::max<uint32_t>(1, static_cast<uint32_t>(bits_per_key_ * 0.69));
}

uint64_t BloomFilter::Rotate(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

uint64_t BloomFilter::Hash64(const std::string& key) {
    const uint64_t prime = 0x9ddfea08eb382d69ULL;
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : key) {
        hash ^= c;
        hash *= prime;
        hash = Rotate(hash, 31);
    }
    hash ^= key.size();
    hash *= prime;
    return hash;
}

void BloomFilter::Add(const std::string& key) {
    if (bits_.empty()) {
        size_t bits = std::max<size_t>(64, key.size() * bits_per_key_);
        size_t bytes = (bits + 7) / 8;
        bits_.assign(bytes, 0);
    }
    uint64_t hash = Hash64(key);
    uint64_t delta = Rotate(hash, 17) | 1;
    size_t bit_count = bits_.size() * 8;
    for (uint32_t i = 0; i < num_hashes_; ++i) {
        size_t bit = hash % bit_count;
        bits_[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
        hash += delta;
    }
    ++key_count_;
}

bool BloomFilter::MayContain(const std::string& key) const {
    if (bits_.empty()) return false;
    uint64_t hash = Hash64(key);
    uint64_t delta = Rotate(hash, 17) | 1;
    size_t bit_count = bits_.size() * 8;
    for (uint32_t i = 0; i < num_hashes_; ++i) {
        size_t bit = hash % bit_count;
        if (!(bits_[bit / 8] & static_cast<uint8_t>(1u << (bit % 8)))) return false;
        hash += delta;
    }
    return true;
}

std::vector<uint8_t> BloomFilter::Serialize() const {
    std::vector<uint8_t> out;
    uint32_t bit_count = static_cast<uint32_t>(bits_.size() * 8);
    uint32_t bytes = static_cast<uint32_t>(bits_.size());
    out.resize(4 + 4 + 4 + bytes);
    auto write_le32 = [&](uint32_t v, size_t off) {
        out[off] = static_cast<uint8_t>(v & 0xFF);
        out[off+1] = static_cast<uint8_t>((v>>8) & 0xFF);
        out[off+2] = static_cast<uint8_t>((v>>16) & 0xFF);
        out[off+3] = static_cast<uint8_t>((v>>24) & 0xFF);
    };
    write_le32(bit_count, 0);
    write_le32(num_hashes_, 4);
    write_le32(bytes, 8);
    std::memcpy(out.data()+12, bits_.data(), bytes);
    return out;
}

BloomFilter BloomFilter::Deserialize(std::span<const uint8_t> data) {
    if (data.size() < 12) throw std::runtime_error("bad bloom filter data");
    auto read32 = [&](size_t off) {
        return static_cast<uint32_t>(data[off]) |
               (static_cast<uint32_t>(data[off+1]) << 8) |
               (static_cast<uint32_t>(data[off+2]) << 16) |
               (static_cast<uint32_t>(data[off+3]) << 24);
    };
    uint32_t bit_count = read32(0);
    uint32_t num_hashes = read32(4);
    uint32_t bytes = read32(8);
    if (data.size() != 12 + bytes) throw std::runtime_error("bad bloom filter size");
    BloomFilter bf;
    bf.bits_per_key_ = bit_count / 1; // approximate
    bf.num_hashes_ = num_hashes;
    bf.bits_.assign(data.begin() + 12, data.end());
    return bf;
}

size_t BloomFilter::ByteSize() const {
    return 12 + bits_.size();
}

} // namespace stratadb
