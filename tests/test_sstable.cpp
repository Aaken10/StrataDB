#include "stratadb/sstable.h"
#include <iostream>
#include <cassert>
#include <filesystem>

using namespace stratadb;
namespace fs = std::filesystem;

int main() {
    std::string path = "test.sst";
    if (fs::exists(path)) fs::remove(path);

    std::vector<SSTable::Entry> entries = {
        {"apple", "red", WalRecordType::Put},
        {"banana", "yellow", WalRecordType::Put},
        {"carrot", "orange", WalRecordType::Put},
        {"durian", "spiky", WalRecordType::Put},
        {"banana", "deleted", WalRecordType::Delete},
    };

    bool ok = SSTable::Create(path, entries);
    if (!ok) {
        std::cerr << "failed to create sstable\n";
        return 1;
    }

    auto table = SSTable::Open(path);
    if (!table) {
        std::cerr << "failed to open sstable\n";
        return 2;
    }

    std::string value;
    ok = table->Get("apple", &value);
    assert(ok && value == "red");

    ok = table->Get("banana", &value);
    assert(!ok);

    ok = table->Get("carrot", &value);
    assert(ok && value == "orange");

    assert(table->ContainsKey("apple"));
    assert(!table->ContainsKey("banana"));
    assert(!table->ContainsKey("fig"));

    std::cout << "sstable test passed\n";
    return 0;
}
