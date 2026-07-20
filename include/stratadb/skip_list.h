#pragma once
#include <string>
#include <vector>
#include <memory>
#include <shared_mutex>
#include <random>
#include <functional>
#include "stratadb/wal.h"

namespace stratadb {

class SkipList {
public:
    explicit SkipList(int max_level = 16);
    ~SkipList();

    // Insert a key/value. Returns true if inserted, false if replaced.
    bool Insert(const std::string& key, const std::string& value,
                WalRecordType type = WalRecordType::Put);

    // Get value for key. Returns true if found.
    bool Get(const std::string& key, std::string* value,
             WalRecordType* type = nullptr) const;

    void ForEach(std::function<void(const std::string&, const std::string&, WalRecordType)> cb) const;

    size_t Size() const;

private:
    struct Node {
        std::string key;
        std::string value;
        WalRecordType type = WalRecordType::Put;
        std::vector<Node*> next;
        Node(int level) : next(level, nullptr) {}
    };

    Node* head_;
    int max_level_;
    int cur_level_;
    mutable std::shared_mutex mu_;
    std::mt19937_64 rng_;
    std::uniform_int_distribution<int> dist_;
    size_t size_;

    int RandomLevel();
};

} // namespace stratadb
