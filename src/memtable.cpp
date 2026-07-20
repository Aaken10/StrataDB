#include "stratadb/memtable.h"

namespace stratadb {

MemTable::MemTable(size_t arena_block)
    : skiplist_(16), arena_(std::make_unique<Arena>(arena_block)) {}

MemTable::~MemTable() = default;

void MemTable::Insert(const std::string& key, const std::string& value, WalRecordType type) {
    // For prototype, directly insert into skiplist. Arena is reserved for node
    // storage later.
    skiplist_.Insert(key, value, type);
}

bool MemTable::Get(const std::string& key, std::string* value, WalRecordType* type) const {
    return skiplist_.Get(key, value, type);
}

void MemTable::Snapshot(std::vector<WalRecord>& out) const {
    out.clear();
    skiplist_.ForEach([&out](const std::string& key, const std::string& value, WalRecordType type) {
        WalRecord rec;
        rec.type = type;
        rec.key = key;
        rec.value = value;
        out.push_back(std::move(rec));
    });
}

size_t MemTable::Size() const {
    return skiplist_.Size();
}

} // namespace stratadb
