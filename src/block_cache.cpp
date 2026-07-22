#include "stratadb/block_cache.h"
#include <algorithm>

namespace stratadb {

BlockCache::BlockCache(size_t max_bytes)
    : max_bytes_(max_bytes) {}

bool BlockCache::Get(const std::string& sstable_path, uint64_t offset, CachedBlock* out) {
    std::lock_guard lock(mu_);
    CacheKey key{sstable_path, offset};
    auto it = map_.find(key);
    if (it == map_.end()) {
        ++miss_count_;
        return false;
    }
    ++hit_count_;
    lru_.splice(lru_.begin(), lru_, it->second.second);
    *out = std::move(it->second.first.block);
    return true;
}

void BlockCache::Put(const std::string& sstable_path, uint64_t offset, CachedBlock block) {
    std::lock_guard lock(mu_);
    CacheKey key{sstable_path, offset};
    size_t entry_bytes = block.data.size();

    auto it = map_.find(key);
    if (it != map_.end()) {
        cur_bytes_ -= it->second.first.byte_size;
        lru_.erase(it->second.second);
        map_.erase(it);
    }

    while (cur_bytes_ + entry_bytes > max_bytes_ && !lru_.empty()) {
        CacheKey victim = lru_.back();
        lru_.pop_back();
        auto vit = map_.find(victim);
        if (vit != map_.end()) {
            cur_bytes_ -= vit->second.first.byte_size;
            map_.erase(vit);
        }
    }

    lru_.push_front(key);
    auto it2 = lru_.begin();
    CacheEntry entry;
    entry.block = std::move(block);
    entry.byte_size = entry_bytes;
    cur_bytes_ += entry_bytes;
    map_[key] = {std::move(entry), it2};
}

void BlockCache::Invalidate(const std::string& sstable_path) {
    std::lock_guard lock(mu_);
    for (auto it = map_.begin(); it != map_.end(); ) {
        if (it->first.sstable_path == sstable_path) {
            cur_bytes_ -= it->second.first.byte_size;
            lru_.erase(it->second.second);
            it = map_.erase(it);
        } else {
            ++it;
        }
    }
}

void BlockCache::Clear() {
    std::lock_guard lock(mu_);
    lru_.clear();
    map_.clear();
    cur_bytes_ = 0;
}

size_t BlockCache::Size() const {
    std::lock_guard lock(mu_);
    return cur_bytes_;
}

size_t BlockCache::HitCount() const {
    std::lock_guard lock(mu_);
    return hit_count_;
}

size_t BlockCache::MissCount() const {
    std::lock_guard lock(mu_);
    return miss_count_;
}

} // namespace stratadb
