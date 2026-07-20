#include "stratadb/arena.h"
#include <cstdlib>
#include <cstring>

namespace stratadb {

Arena::Arena(size_t block_size)
    : block_size_(block_size), offset_(0) {
    blocks_.push_back(static_cast<char*>(std::malloc(block_size_)));
}

Arena::~Arena() {
    for (char* b : blocks_) std::free(b);
    blocks_.clear();
}

void* Arena::Alloc(size_t bytes) {
    if (bytes == 0) return nullptr;
    std::lock_guard<std::mutex> lg(mu_);
    if (bytes > block_size_) {
        // large allocation: allocate separate block
        char* b = static_cast<char*>(std::malloc(bytes));
        blocks_.push_back(b);
        return b;
    }
    if (!blocks_.size()) {
        blocks_.push_back(static_cast<char*>(std::malloc(block_size_)));
        offset_ = 0;
    }
    if (offset_ + bytes > block_size_) {
        blocks_.push_back(static_cast<char*>(std::malloc(block_size_)));
        offset_ = 0;
    }
    char* res = blocks_.back() + offset_;
    offset_ += bytes;
    return static_cast<void*>(res);
}

void Arena::Reset() {
    std::lock_guard<std::mutex> lg(mu_);
    for (size_t i = 0; i + 1 < blocks_.size(); ++i) std::free(blocks_[i]);
    if (!blocks_.empty()) {
        // keep one block
        offset_ = 0;
        char* last = blocks_.back();
        blocks_.clear();
        blocks_.push_back(last);
    }
}

} // namespace stratadb
