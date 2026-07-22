#pragma once
#include "stratadb/block_cache.h"
#include "stratadb/memtable.h"
#include "stratadb/sstable.h"
#include "stratadb/wal.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace stratadb {

class DB {
public:
    struct Options {
        std::string path = ".";
        size_t memtable_threshold = 1 << 16;
        size_t max_sstable_count = 4;
        bool background_flush = true;
        size_t flush_interval_ms = 1000;
        size_t block_cache_size = 64 * 1024 * 1024;
    };

    static std::unique_ptr<DB> Open(const Options& options);

    ~DB();

    bool Put(const std::string& key, const std::string& value);
    bool Delete(const std::string& key);
    bool Get(const std::string& key, std::string* value) const;
    bool Compact();
    bool Close();

private:
    explicit DB(const Options& options);

    bool LoadSSTables();
    bool FlushMemTable();
    bool CompactInternal();
    std::string MakeSSTablePath(uint64_t id) const;
    static uint64_t ParseSSTableId(const std::string& filename);

    void BackgroundFlusher();
    void StopBackgroundThread();

    std::string path_;
    std::string wal_path_;
    Options options_;
    std::unique_ptr<WAL> wal_;
    std::unique_ptr<MemTable> memtable_;
    std::vector<std::unique_ptr<SSTable>> sstables_;
    uint64_t next_sstable_id_ = 0;
    mutable std::mutex mu_;

    std::atomic<bool> bg_running_{false};
    std::thread bg_thread_;
    std::condition_variable bg_cv_;
    std::mutex bg_mu_;
    bool flush_requested_ = false;

    BlockCache block_cache_;
};

} // namespace stratadb
