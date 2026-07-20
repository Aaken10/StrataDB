#include "stratadb/memtable.h"

namespace stratadb {

MemTable::MemTable(size_t arena_block)
    : skiplist_(16), arena_(std::make_unique<Arena>(arena_block)) {}

MemTable::~MemTable() = default;

void MemTable::Insert(const std::string& key, const std::string& value) {
    // For prototype, directly insert into skiplist. Arena is reserved for node storage later.
    skiplist_.Insert(key, value);
}

bool MemTable::Get(const std::string& key, std::string* value) const {
    return skiplist_.Get(key, value);
}

size_t MemTable::Size() const { return skiplist_.Size(); }

} // namespace stratadb
