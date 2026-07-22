#pragma once
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace stratadb {

struct CachedBlock {
    std::vector<uint8_t> data;
};

class BlockCache {
public:
    explicit BlockCache(size_t max_bytes = 64 * 1024 * 1024);
    ~BlockCache() = default;

    bool Get(const std::string& sstable_path, uint64_t offset, CachedBlock* out);
    void Put(const std::string& sstable_path, uint64_t offset, CachedBlock block);
    void Invalidate(const std::string& sstable_path);
    void Clear();

    size_t Size() const;
    size_t HitCount() const;
    size_t MissCount() const;

private:
    struct CacheKey {
        std::string sstable_path;
        uint64_t offset;
        bool operator==(const CacheKey& o) const {
            return sstable_path == o.sstable_path && offset == o.offset;
        }
    };

    struct CacheKeyHash {
        size_t operator()(const CacheKey& k) const {
            size_t h1 = std::hash<std::string>{}(k.sstable_path);
            size_t h2 = std::hash<uint64_t>{}(k.offset);
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL);
        }
    };

    struct CacheEntry {
        CachedBlock block;
        size_t byte_size;
    };

    using LruList = std::list<CacheKey>;

    mutable std::mutex mu_;
    size_t max_bytes_;
    size_t cur_bytes_ = 0;
    size_t hit_count_ = 0;
    size_t miss_count_ = 0;

    LruList lru_;
    std::unordered_map<CacheKey, std::pair<CacheEntry, LruList::iterator>, CacheKeyHash> map_;
};

} // namespace stratadb
