#pragma once
#include "stratadb/skip_list.h"
#include "stratadb/arena.h"
#include <string>
#include <memory>

namespace stratadb {

class MemTable {
public:
    explicit MemTable(size_t arena_block = 1<<20);
    ~MemTable();

    // Insert key/value
    void Insert(const std::string& key, const std::string& value);

    // Get value for key. Returns true if found.
    bool Get(const std::string& key, std::string* value) const;

    size_t Size() const;

private:
    SkipList skiplist_;
    std::unique_ptr<Arena> arena_;
};

} // namespace stratadb
