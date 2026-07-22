#include "stratadb/db.h"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

namespace stratadb {

namespace fs = std::filesystem;

DB::DB(const Options& options)
    : path_(options.path), wal_path_((fs::path(path_) / "db.wal").string()), options_(options) {
    fs::create_directories(path_);
    memtable_ = std::make_unique<MemTable>();
}

DB::~DB() {
    Close();
}

std::unique_ptr<DB> DB::Open(const Options& options) {
    auto db = std::unique_ptr<DB>(new DB(options));
    if (!db->LoadSSTables()) return nullptr;
    try {
        db->wal_ = std::make_unique<WAL>(db->wal_path_);
    } catch (...) {
        return nullptr;
    }

    if (fs::exists(db->wal_path_)) {
        if (!WAL::Replay(db->wal_path_, [db = db.get()](const WalRecord& r) {
                if (r.type == WalRecordType::Put) {
                    db->memtable_->Insert(r.key, r.value, r.type);
                } else {
                    db->memtable_->Insert(r.key, std::string(), r.type);
                }
            })) {
            return nullptr;
        }
    }

    if (options.background_flush) {
        db->bg_running_ = true;
        db->bg_thread_ = std::thread(&DB::BackgroundFlusher, db.get());
    }

    return db;
}

bool DB::LoadSSTables() {
    std::vector<fs::path> paths;
    for (auto const& entry : fs::directory_iterator(path_)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".sst") continue;
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    next_sstable_id_ = 0;
    for (auto const& path : paths) {
        auto table = SSTable::Open(path.string());
        if (!table) return false;
        uint64_t parsed = ParseSSTableId(path.filename().string());
        next_sstable_id_ = std::max(next_sstable_id_, parsed + 1);
        sstables_.push_back(std::move(table));
    }
    return true;
}

uint64_t DB::ParseSSTableId(const std::string& filename) {
    static constexpr const char prefix[] = "sstable_";
    static constexpr size_t prefix_len = sizeof(prefix) - 1;
    static constexpr size_t suffix_len = 4; // .sst
    if (filename.size() <= prefix_len + suffix_len) return 0;
    if (filename.rfind(prefix, 0) != 0) return 0;
    if (filename.substr(filename.size() - suffix_len) != ".sst") return 0;
    std::string number = filename.substr(prefix_len, filename.size() - prefix_len - suffix_len);
    try {
        return std::stoull(number);
    } catch (...) {
        return 0;
    }
}

std::string DB::MakeSSTablePath(uint64_t id) const {
    std::ostringstream oss;
    oss << "sstable_" << std::setfill('0') << std::setw(20) << id << ".sst";
    return (fs::path(path_) / oss.str()).string();
}

bool DB::Put(const std::string& key, const std::string& value) {
    std::lock_guard lock(mu_);
    WalRecord rec;
    rec.type = WalRecordType::Put;
    rec.key = key;
    rec.value = value;
    if (!wal_->Append(rec)) return false;
    if (!wal_->Sync()) return false;
    memtable_->Insert(key, value, WalRecordType::Put);
    if (memtable_->Size() >= options_.memtable_threshold) {
        return FlushMemTable();
    }
    return true;
}

bool DB::Delete(const std::string& key) {
    std::lock_guard lock(mu_);
    WalRecord rec;
    rec.type = WalRecordType::Delete;
    rec.key = key;
    rec.value.clear();
    if (!wal_->Append(rec)) return false;
    if (!wal_->Sync()) return false;
    memtable_->Insert(key, std::string(), WalRecordType::Delete);
    if (memtable_->Size() >= options_.memtable_threshold) {
        return FlushMemTable();
    }
    return true;
}

bool DB::Get(const std::string& key, std::string* value) const {
    std::lock_guard lock(mu_);
    std::string stored_value;
    WalRecordType type;
    if (memtable_->Get(key, &stored_value, &type)) {
        if (type == WalRecordType::Delete) return false;
        if (value) *value = stored_value;
        return true;
    }
    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        auto entry = (*it)->Find(key);
        if (!entry.has_value()) continue;
        if (entry->type == WalRecordType::Delete) return false;
        if (value) *value = entry->value;
        return true;
    }
    return false;
}

bool DB::FlushMemTable() {
    if (memtable_->Size() == 0) return true;
    std::vector<WalRecord> wal_entries;
    memtable_->Snapshot(wal_entries);
    if (wal_entries.empty()) return true;

    std::vector<SSTable::Entry> entries;
    entries.reserve(wal_entries.size());
    for (auto const& rec : wal_entries) {
        entries.push_back({rec.key, rec.value, rec.type});
    }

    const std::string path = MakeSSTablePath(next_sstable_id_);
    if (!SSTable::Create(path, entries)) return false;
    auto table = SSTable::Open(path);
    if (!table) return false;
    sstables_.push_back(std::move(table));
    ++next_sstable_id_;

    if (sstables_.size() > options_.max_sstable_count) {
        if (!CompactInternal()) return false;
    }

    memtable_ = std::make_unique<MemTable>();
    wal_->Close();
    if (fs::exists(wal_path_) && !fs::remove(wal_path_)) return false;
    try {
        wal_ = std::make_unique<WAL>(wal_path_);
    } catch (...) {
        return false;
    }
    return true;
}

void DB::BackgroundFlusher() {
    while (bg_running_) {
        {
            std::unique_lock lock(bg_mu_);
            bg_cv_.wait_for(lock, std::chrono::milliseconds(options_.flush_interval_ms),
                [this] { return flush_requested_ || !bg_running_; });
        }

        if (!bg_running_) break;

        {
            std::lock_guard lock(mu_);
            if (flush_requested_ || memtable_->Size() >= options_.memtable_threshold) {
                FlushMemTable();
                flush_requested_ = false;
            }
        }
    }

    std::lock_guard lock(mu_);
    FlushMemTable();
}

void DB::StopBackgroundThread() {
    if (bg_running_) {
        bg_running_ = false;
        bg_cv_.notify_all();
        if (bg_thread_.joinable()) bg_thread_.join();
    }
}

bool DB::Close() {
    StopBackgroundThread();
    std::lock_guard lock(mu_);
    if (wal_) {
        FlushMemTable();
        wal_->Close();
        wal_.reset();
    }
    return true;
}

bool DB::Compact() {
    std::lock_guard lock(mu_);
    return CompactInternal();
}

bool DB::CompactInternal() {
    if (sstables_.empty()) return true;

    std::map<std::string, SSTable::Entry> merged;
    for (auto const& table : sstables_) {
        for (auto const& entry : table->ReadAllEntries()) {
            merged[entry.key] = entry;
        }
    }

    std::vector<SSTable::Entry> live_entries;
    live_entries.reserve(merged.size());
    for (auto const& [key, entry] : merged) {
        if (entry.type == WalRecordType::Put) {
            live_entries.push_back(entry);
        }
    }

    std::vector<std::string> old_paths;
    old_paths.reserve(sstables_.size());
    for (auto const& table : sstables_) {
        old_paths.push_back(table->Path());
    }
    sstables_.clear();

    for (auto const& path : old_paths) {
        fs::remove(path);
    }

    if (!live_entries.empty()) {
        const std::string path = MakeSSTablePath(next_sstable_id_);
        if (!SSTable::Create(path, live_entries)) return false;
        auto compacted = SSTable::Open(path);
        if (!compacted) return false;
        sstables_.push_back(std::move(compacted));
        ++next_sstable_id_;
    }

    return true;
}

} // namespace stratadb

