#pragma once
#include "stratadb/memtable.h"
#include "stratadb/sstable.h"
#include "stratadb/wal.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace stratadb {

class DB {
public:
    struct Options {
        std::string path = ".";
        size_t memtable_threshold = 1 << 16;
    };

    static std::unique_ptr<DB> Open(const Options& options);

    ~DB();

    bool Put(const std::string& key, const std::string& value);
    bool Delete(const std::string& key);
    bool Get(const std::string& key, std::string* value) const;
    bool Close();

private:
    explicit DB(const Options& options);

    bool LoadSSTables();
    bool FlushMemTable();
    std::string MakeSSTablePath(uint64_t id) const;
    static uint64_t ParseSSTableId(const std::string& filename);

    std::string path_;
    std::string wal_path_;
    Options options_;
    std::unique_ptr<WAL> wal_;
    std::unique_ptr<MemTable> memtable_;
    std::vector<std::unique_ptr<SSTable>> sstables_;
    uint64_t next_sstable_id_ = 0;
    mutable std::mutex mu_;
};

} // namespace stratadb
