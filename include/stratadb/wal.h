#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <functional>

namespace stratadb {

enum class WalRecordType : uint8_t { Put = 1, Delete = 2 };

struct WalRecord {
    WalRecordType type;
    std::string key;
    std::string value;
};

class WAL {
public:
    struct Options {
        size_t sync_interval_ms = 1;
        size_t batch_size = 64;
    };

    explicit WAL(const std::string& path, const Options& opts = Options{});
    ~WAL();

    bool Append(const WalRecord& rec);
    bool Sync();
    void Close();

    static bool Replay(const std::string& path, std::function<void(const WalRecord&)> cb);

    static std::vector<uint8_t> SerializeRecord(const WalRecord& rec);

private:
    void BackgroundSyncer();
    void StopBackgroundThread();
    bool FlushBuffer();

    std::string path_;
    FILE* f_ = nullptr;
    Options opts_;

    std::vector<uint8_t> write_buf_;
    size_t pending_count_ = 0;
    std::mutex buf_mu_;

    std::atomic<bool> sync_running_{false};
    std::thread sync_thread_;
    std::mutex sync_mu_;
    std::condition_variable sync_cv_;
};

} // namespace stratadb
