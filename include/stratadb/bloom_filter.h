#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <span>

namespace stratadb {

class BloomFilter {
public:
    BloomFilter();
    BloomFilter(size_t bits_per_key);
    void Add(const std::string& key);
    bool MayContain(const std::string& key) const;
    std::vector<uint8_t> Serialize() const;
    static BloomFilter Deserialize(std::span<const uint8_t> data);
    size_t ByteSize() const;

private:
    size_t bits_per_key_;
    uint32_t num_hashes_;
    std::vector<uint8_t> bits_;
    size_t key_count_;

    static uint64_t Hash64(const std::string& key);
    static uint64_t Rotate(uint64_t x, int r);
};

} // namespace stratadb

