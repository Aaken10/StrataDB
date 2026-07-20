#pragma once
#include "stratadb/skip_list.h"
#include "stratadb/arena.h"
#include "stratadb/wal.h"
#include <string>
#include <memory>
#include <vector>

namespace stratadb {

class MemTable {
public:
    explicit MemTable(size_t arena_block = 1<<20);
    ~MemTable();

    // Insert key/value record.
    void Insert(const std::string& key, const std::string& value,
                WalRecordType type = WalRecordType::Put);

    // Get value for key. Returns true if found.
    bool Get(const std::string& key, std::string* value,
             WalRecordType* type = nullptr) const;

    void Snapshot(std::vector<WalRecord>& out) const;

    size_t Size() const;

private:
    SkipList skiplist_;
    std::unique_ptr<Arena> arena_;
};

} // namespace stratadb
