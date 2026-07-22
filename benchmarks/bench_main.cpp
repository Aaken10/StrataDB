#include "stratadb/db.h"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace stratadb;

static std::string MakeKey(int n) {
    return "key_" + std::to_string(n);
}

static std::string MakeValue(int n) {
    return "value_" + std::to_string(n);
}

static const std::string kBenchPath = "bench_db";
static const int kSmallN = 1000;
static const int kLargeN = 100000;

static void BM_Put_Sequential(benchmark::State& state) {
    std::filesystem::remove_all(kBenchPath);
    DB::Options opts;
    opts.path = kBenchPath;
    opts.memtable_threshold = 1 << 16;
    opts.background_flush = false;
    auto db = DB::Open(opts);

    int i = 0;
    for (auto _ : state) {
        db->Put(MakeKey(i), MakeValue(i));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
    db->Close();
    std::filesystem::remove_all(kBenchPath);
}
BENCHMARK(BM_Put_Sequential)->Unit(benchmark::kMicrosecond);

static void BM_Put_Random(benchmark::State& state) {
    std::filesystem::remove_all(kBenchPath);
    DB::Options opts;
    opts.path = kBenchPath;
    opts.memtable_threshold = 1 << 20;
    opts.background_flush = false;
    auto db = DB::Open(opts);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, kLargeN * 10);
    int i = 0;
    for (auto _ : state) {
        db->Put(MakeKey(dist(rng)), MakeValue(i));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
    db->Close();
    std::filesystem::remove_all(kBenchPath);
}
BENCHMARK(BM_Put_Random)->Unit(benchmark::kMicrosecond);

static void BM_Get_MemTable(benchmark::State& state) {
    std::filesystem::remove_all(kBenchPath);
    DB::Options opts;
    opts.path = kBenchPath;
    opts.memtable_threshold = 1 << 20;
    opts.background_flush = false;
    auto db = DB::Open(opts);

    for (int i = 0; i < kSmallN; ++i) {
        db->Put(MakeKey(i), MakeValue(i));
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, kSmallN - 1);
    for (auto _ : state) {
        std::string val;
        db->Get(MakeKey(dist(rng)), &val);
    }
    state.SetItemsProcessed(state.iterations());
    db->Close();
    std::filesystem::remove_all(kBenchPath);
}
BENCHMARK(BM_Get_MemTable)->Unit(benchmark::kMicrosecond);

static void BM_Get_SSTable(benchmark::State& state) {
    std::filesystem::remove_all(kBenchPath);
    DB::Options opts;
    opts.path = kBenchPath;
    opts.memtable_threshold = 2;
    opts.max_sstable_count = 1000;
    opts.background_flush = false;
    auto db = DB::Open(opts);

    for (int i = 0; i < kSmallN; ++i) {
        db->Put(MakeKey(i), MakeValue(i));
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, kSmallN - 1);
    for (auto _ : state) {
        std::string val;
        db->Get(MakeKey(dist(rng)), &val);
    }
    state.SetItemsProcessed(state.iterations());
    db->Close();
    std::filesystem::remove_all(kBenchPath);
}
BENCHMARK(BM_Get_SSTable)->Unit(benchmark::kMicrosecond);

static void BM_Get_SSTable_Cached(benchmark::State& state) {
    std::filesystem::remove_all(kBenchPath);
    DB::Options opts;
    opts.path = kBenchPath;
    opts.memtable_threshold = 2;
    opts.max_sstable_count = 1000;
    opts.background_flush = false;
    opts.block_cache_size = 64 * 1024 * 1024;
    auto db = DB::Open(opts);

    for (int i = 0; i < kSmallN; ++i) {
        db->Put(MakeKey(i), MakeValue(i));
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, kSmallN - 1);
    for (int i = 0; i < 1000; ++i) {
        std::string val;
        db->Get(MakeKey(dist(rng)), &val);
    }

    for (auto _ : state) {
        std::string val;
        db->Get(MakeKey(dist(rng)), &val);
    }
    state.SetItemsProcessed(state.iterations());
    db->Close();
    std::filesystem::remove_all(kBenchPath);
}
BENCHMARK(BM_Get_SSTable_Cached)->Unit(benchmark::kMicrosecond);

static void BM_Compact(benchmark::State& state) {
    for (auto _ : state) {
        std::filesystem::remove_all(kBenchPath);
        DB::Options opts;
        opts.path = kBenchPath;
        opts.memtable_threshold = 2;
        opts.max_sstable_count = 1;
        opts.background_flush = false;
        auto db = DB::Open(opts);

        for (int i = 0; i < 500; ++i) {
            db->Put(MakeKey(i), MakeValue(i));
        }
        db->Compact();
        db->Close();
        std::filesystem::remove_all(kBenchPath);
    }
}
BENCHMARK(BM_Compact)->Unit(benchmark::kMillisecond);

static void BM_Delete_Sequential(benchmark::State& state) {
    std::filesystem::remove_all(kBenchPath);
    DB::Options opts;
    opts.path = kBenchPath;
    opts.memtable_threshold = 1 << 20;
    opts.background_flush = false;
    auto db = DB::Open(opts);

    for (int i = 0; i < kSmallN; ++i) {
        db->Put(MakeKey(i), MakeValue(i));
    }

    int i = 0;
    for (auto _ : state) {
        db->Delete(MakeKey(i));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
    db->Close();
    std::filesystem::remove_all(kBenchPath);
}
BENCHMARK(BM_Delete_Sequential)->Unit(benchmark::kMicrosecond);

static void BM_PutGet_Mixed(benchmark::State& state) {
    std::filesystem::remove_all(kBenchPath);
    DB::Options opts;
    opts.path = kBenchPath;
    opts.memtable_threshold = 1 << 16;
    opts.background_flush = false;
    auto db = DB::Open(opts);

    for (int i = 0; i < kSmallN; ++i) {
        db->Put(MakeKey(i), MakeValue(i));
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, kSmallN - 1);
    std::uniform_int_distribution<int> op_dist(0, 1);
    int i = kSmallN;
    for (auto _ : state) {
        if (op_dist(rng) == 0) {
            db->Put(MakeKey(i), MakeValue(i));
            ++i;
        } else {
            std::string val;
            db->Get(MakeKey(dist(rng)), &val);
        }
    }
    state.SetItemsProcessed(state.iterations());
    db->Close();
    std::filesystem::remove_all(kBenchPath);
}
BENCHMARK(BM_PutGet_Mixed)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
