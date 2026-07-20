#include "stratadb/db.h"
#include <filesystem>
#include <iostream>

using namespace stratadb;
namespace fs = std::filesystem;

int main() {
    const fs::path test_path = "test_db";
    if (fs::exists(test_path)) fs::remove_all(test_path);

    DB::Options opts;
    opts.path = test_path.string();
    opts.memtable_threshold = 10;

    auto db = DB::Open(opts);
    if (!db) {
        std::cerr << "db open failed\n";
        return 1;
    }

    for (int i = 0; i < 12; ++i) {
        if (!db->Put("k" + std::to_string(i), "v" + std::to_string(i))) {
            std::cerr << "put failed\n";
            return 2;
        }
    }

    if (!db->Delete("k5")) {
        std::cerr << "delete failed\n";
        return 3;
    }

    std::string value;
    if (!db->Get("k0", &value) || value != "v0") {
        std::cerr << "get k0 failed\n";
        return 4;
    }
    if (db->Get("k5", &value)) {
        std::cerr << "deleted key returned\n";
        return 5;
    }
    if (!db->Get("k11", &value) || value != "v11") {
        std::cerr << "get k11 failed\n";
        return 6;
    }

    db->Close();
    db.reset();

    auto reopened = DB::Open(opts);
    if (!reopened) {
        std::cerr << "reopen failed\n";
        return 7;
    }

    if (!reopened->Get("k0", &value) || value != "v0") {
        std::cerr << "reopen get k0 failed\n";
        return 8;
    }
    if (reopened->Get("k5", &value)) {
        std::cerr << "reopen deleted key returned\n";
        return 9;
    }
    if (!reopened->Get("k11", &value) || value != "v11") {
        std::cerr << "reopen get k11 failed\n";
        return 10;
    }

    std::cout << "db integration test passed\n";
    return 0;
}
