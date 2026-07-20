#pragma once
#include <cstddef>
#include <vector>
#include <mutex>

namespace stratadb {

class Arena {
public:
    explicit Arena(size_t block_size = 1 << 20);
    ~Arena();

    // Allocates `bytes` bytes. Thread-safe.
    void* Alloc(size_t bytes);

    // Reset and free blocks (not thread-safe; call when no concurrent users)
    void Reset();

    size_t BlockSize() const { return block_size_; }

private:
    size_t block_size_;
    std::vector<char*> blocks_;
    size_t offset_;
    std::mutex mu_;
};

} // namespace stratadb
