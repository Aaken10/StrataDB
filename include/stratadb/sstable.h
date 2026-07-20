#pragma once
#include "stratadb/bloom_filter.h"
#include "stratadb/wal.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace stratadb {

class SSTable {
public:
    struct Entry {
        std::string key;
        std::string value;
        WalRecordType type;
    };

    static bool Create(const std::string& path, const std::vector<Entry>& entries);
    static std::unique_ptr<SSTable> Open(const std::string& path);

    bool Get(const std::string& key, std::string* value) const;
    bool ContainsKey(const std::string& key) const;
    std::optional<Entry> Find(const std::string& key) const;
    std::vector<Entry> ReadAllEntries() const;
    const std::string& Path() const;

private:
    struct IndexEntry {
        std::string max_key;
        uint64_t block_offset;
        uint32_t block_size;
    };

    std::string path_;
    BloomFilter filter_;
    std::vector<IndexEntry> index_;
    uint64_t data_start_offset_;

    explicit SSTable(const std::string& path);
    bool Load();
    std::optional<Entry> FindInBlock(uint64_t offset, uint32_t size, const std::string& key) const;
};

} // namespace stratadb
