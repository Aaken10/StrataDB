#pragma once
#include <string>
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
    explicit WAL(const std::string& path);
    ~WAL();

    // Append a record to WAL (durability controlled by caller via Sync)
    bool Append(const WalRecord& rec);
    bool Sync();
    void Close();

    // Replay WAL file and invoke callback for each record
    static bool Replay(const std::string& path, std::function<void(const WalRecord&)> cb);

private:
    std::string path_;
    FILE* f_ = nullptr;
};

} // namespace stratadb
