#include "stratadb/memtable.h"
#include "stratadb/wal.h"
#include <thread>
#include <vector>
#include <iostream>
#include <cassert>
#include <filesystem>

using namespace stratadb;
namespace fs = std::filesystem;

void concurrent_puts(MemTable& mt, int start, int n) {
    for (int i = 0; i < n; ++i) {
        int k = start + i;
        mt.Insert(std::to_string(k), "v" + std::to_string(k));
    }
}

int main() {
    // Concurrent Put/Get test on MemTable
    MemTable mt;
    const int T = 4; const int N = 1000;
    std::vector<std::thread> writers;
    for (int t = 0; t < T; ++t) writers.emplace_back(concurrent_puts, std::ref(mt), t*N, N);
    for (auto& th : writers) th.join();
    size_t expected = static_cast<size_t>(T) * N;
    if (mt.Size() != expected) { std::cerr << "memtable size mismatch\n"; return 2; }

    // WAL append + replay (crash recovery) test
    std::string walpath = "test.wal";
    if (fs::exists(walpath)) fs::remove(walpath);
    WAL wal(walpath);
    for (int i = 0; i < 500; ++i) {
        WalRecord r; r.type = WalRecordType::Put; r.key = "k" + std::to_string(i); r.value = "val" + std::to_string(i);
        if (!wal.Append(r)) { std::cerr << "wal append failed\n"; return 3; }
    }
    wal.Sync(); wal.Close();

    MemTable recovered;
    bool ok = WAL::Replay(walpath, [&recovered](const WalRecord& r) {
        if (r.type == WalRecordType::Put) recovered.Insert(r.key, r.value);
    });
    if (!ok) { std::cerr << "wal replay failed\n"; return 4; }
    for (int i = 0; i < 500; ++i) {
        std::string v; bool f = recovered.Get("k" + std::to_string(i), &v);
        if (!f || v != ("val" + std::to_string(i))) { std::cerr << "replay mismatch for " << i << "\n"; return 5; }
    }

    std::cout << "memtable+wal test passed\n";
    return 0;
}
