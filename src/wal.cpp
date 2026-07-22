#include "stratadb/wal.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cassert>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace stratadb {

static uint32_t crc32_table[256];
static bool crc_table_inited = false;

static void init_crc32() {
    if (crc_table_inited) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            if (c & 1) c = 0xEDB88320u ^ (c >> 1);
            else c = c >> 1;
        }
        crc32_table[i] = c;
    }
    crc_table_inited = true;
}

static uint32_t crc32(const void* data, size_t n) {
    init_crc32();
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = crc32_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

WAL::WAL(const std::string& path, const Options& opts)
    : path_(path), opts_(opts) {
    f_ = std::fopen(path.c_str(), "ab+");
    if (!f_) throw std::runtime_error("failed to open wal");
    write_buf_.reserve(opts_.batch_size * 128);

    sync_running_ = true;
    sync_thread_ = std::thread(&WAL::BackgroundSyncer, this);
}

WAL::~WAL() { Close(); }

std::vector<uint8_t> WAL::SerializeRecord(const WalRecord& rec) {
    uint32_t key_len = static_cast<uint32_t>(rec.key.size());
    uint32_t val_len = static_cast<uint32_t>(rec.value.size());
    uint8_t type = static_cast<uint8_t>(rec.type);

    std::vector<uint8_t> buf;
    buf.resize(4 + 4 + 4 + 1 + key_len + val_len);
    size_t off = 4;
    auto write_le32 = [&](uint32_t v) {
        buf[off+0] = v & 0xFF; buf[off+1] = (v>>8)&0xFF; buf[off+2] = (v>>16)&0xFF; buf[off+3] = (v>>24)&0xFF; off += 4;
    };
    write_le32(key_len);
    write_le32(val_len);
    buf[off++] = type;
    if (key_len) memcpy(buf.data()+off, rec.key.data(), key_len);
    off += key_len;
    if (val_len) memcpy(buf.data()+off, rec.value.data(), val_len);
    off += val_len;

    uint32_t c = crc32(buf.data()+4, buf.size()-4);
    buf[0] = c & 0xFF; buf[1] = (c>>8)&0xFF; buf[2] = (c>>16)&0xFF; buf[3] = (c>>24)&0xFF;
    return buf;
}

bool WAL::Append(const WalRecord& rec) {
    if (!f_) return false;

    std::vector<uint8_t> serialized = SerializeRecord(rec);

    std::vector<uint8_t> to_write;
    {
        std::lock_guard lock(buf_mu_);
        write_buf_.insert(write_buf_.end(), serialized.begin(), serialized.end());
        ++pending_count_;

        if (pending_count_ >= opts_.batch_size) {
            to_write.swap(write_buf_);
            pending_count_ = 0;
        }
    }

    if (!to_write.empty()) {
        size_t wrote = std::fwrite(to_write.data(), 1, to_write.size(), f_);
        if (wrote != to_write.size()) return false;
    }

    return true;
}

bool WAL::FlushBuffer() {
    std::vector<uint8_t> to_write;
    {
        std::lock_guard lock(buf_mu_);
        if (pending_count_ == 0) return true;
        to_write.swap(write_buf_);
        pending_count_ = 0;
    }
    size_t wrote = std::fwrite(to_write.data(), 1, to_write.size(), f_);
    return wrote == to_write.size();
}

void WAL::BackgroundSyncer() {
    while (sync_running_) {
        {
            std::unique_lock lock(sync_mu_);
            sync_cv_.wait_for(lock, std::chrono::milliseconds(opts_.sync_interval_ms),
                [this] { return !sync_running_; });
        }
        if (!sync_running_) break;

        std::vector<uint8_t> to_write;
        {
            std::lock_guard lock(buf_mu_);
            if (pending_count_ == 0) continue;
            to_write.swap(write_buf_);
            pending_count_ = 0;
        }

        size_t wrote = std::fwrite(to_write.data(), 1, to_write.size(), f_);
        if (wrote != to_write.size()) continue;
        std::fflush(f_);
#ifdef _WIN32
        int fd = _fileno(f_);
        if (fd >= 0) _commit(fd);
#else
        int fd = fileno(f_);
        if (fd >= 0) fsync(fd);
#endif
    }

    std::vector<uint8_t> to_write;
    {
        std::lock_guard lock(buf_mu_);
        if (pending_count_ > 0) {
            to_write.swap(write_buf_);
            pending_count_ = 0;
        }
    }

    if (!to_write.empty()) {
        std::fwrite(to_write.data(), 1, to_write.size(), f_);
        std::fflush(f_);
#ifdef _WIN32
        int fd = _fileno(f_);
        if (fd >= 0) _commit(fd);
#else
        int fd = fileno(f_);
        if (fd >= 0) fsync(fd);
#endif
    }
}

void WAL::StopBackgroundThread() {
    if (sync_running_) {
        sync_running_ = false;
        sync_cv_.notify_all();
        if (sync_thread_.joinable()) sync_thread_.join();
    }
}

bool WAL::Sync() {
    if (!f_) return false;
    FlushBuffer();
    std::fflush(f_);
#ifdef _WIN32
    int fd = _fileno(f_);
    if (fd >= 0) _commit(fd);
#else
    int fd = fileno(f_);
    if (fd >= 0) fsync(fd);
#endif
    return true;
}

void WAL::Close() {
    StopBackgroundThread();
    if (f_) {
        FlushBuffer();
        std::fflush(f_);
#ifdef _WIN32
        int fd = _fileno(f_);
        if (fd >= 0) _commit(fd);
#else
        int fd = fileno(f_);
        if (fd >= 0) fsync(fd);
#endif
        std::fclose(f_);
        f_ = nullptr;
    }
}

bool WAL::Replay(const std::string& path, std::function<void(const WalRecord&)> cb) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    while (true) {
        uint8_t header[13];
        size_t r = std::fread(header, 1, 13, f);
        if (r == 0) break;
        if (r < 13) { std::fclose(f); return false; }
        uint32_t crc = header[0] | (header[1]<<8) | (header[2]<<16) | (header[3]<<24);
        uint32_t key_len = header[4] | (header[5]<<8) | (header[6]<<16) | (header[7]<<24);
        uint32_t val_len = header[8] | (header[9]<<8) | (header[10]<<16) | (header[11]<<24);
        uint8_t type = header[12];
        std::vector<uint8_t> payload;
        payload.resize(4 + 4 + 1 + key_len + val_len);
        size_t off = 0;
        auto write_le32 = [&](uint32_t v) {
            payload[off+0] = v & 0xFF; payload[off+1] = (v>>8)&0xFF; payload[off+2] = (v>>16)&0xFF; payload[off+3] = (v>>24)&0xFF; off += 4;
        };
        write_le32(key_len);
        write_le32(val_len);
        payload[off++] = type;
        if (key_len) {
            size_t nr = std::fread(payload.data()+off, 1, key_len, f);
            if (nr != key_len) { std::fclose(f); return false; }
            off += key_len;
        }
        if (val_len) {
            size_t nr = std::fread(payload.data()+off, 1, val_len, f);
            if (nr != val_len) { std::fclose(f); return false; }
            off += val_len;
        }
        uint32_t calc = crc32(payload.data(), payload.size());
        if (calc != crc) { std::fclose(f); return false; }

        WalRecord rec;
        rec.type = (type == 1 ? WalRecordType::Put : WalRecordType::Delete);
        size_t key_off = 4 + 4 + 1;
        rec.key.assign(reinterpret_cast<char*>(payload.data()+key_off), key_len);
        rec.value.assign(reinterpret_cast<char*>(payload.data()+key_off+key_len), val_len);
        cb(rec);
    }
    std::fclose(f);
    return true;
}

} // namespace stratadb
