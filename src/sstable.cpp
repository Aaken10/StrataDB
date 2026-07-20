#include "stratadb/sstable.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <filesystem>

namespace stratadb {

static void write_le32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void write_le64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

static uint32_t read_u32(const uint8_t* ptr) {
    return static_cast<uint32_t>(ptr[0]) |
           (static_cast<uint32_t>(ptr[1]) << 8) |
           (static_cast<uint32_t>(ptr[2]) << 16) |
           (static_cast<uint32_t>(ptr[3]) << 24);
}

static uint64_t read_u64(const uint8_t* ptr) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(ptr[i]) << (i * 8);
    return v;
}

SSTable::SSTable(const std::string& path)
    : path_(path), filter_(10), data_start_offset_(0) {}

bool SSTable::Create(const std::string& path, const std::vector<Entry>& entries) {
    if (entries.empty()) return false;
    std::vector<Entry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
        return a.key < b.key;
    });

    std::vector<Entry> unique_entries;
    unique_entries.reserve(sorted.size());
    for (auto const& entry : sorted) {
        if (!unique_entries.empty() && unique_entries.back().key == entry.key) {
            unique_entries.back() = entry;
        } else {
            unique_entries.push_back(entry);
        }
    }

    BloomFilter filter(10);
    for (auto const& entry : unique_entries) filter.Add(entry.key);

    std::vector<uint8_t> data;
    std::vector<IndexEntry> index;
    const uint32_t block_size_limit = 4096;
    uint64_t block_begin = 0;
    uint32_t block_size = 0;
    std::string block_last_key;

    auto flush_block = [&]() {
        if (block_size == 0) return;
        index.push_back({block_last_key, block_begin, block_size});
        block_begin += block_size;
        block_size = 0;
        block_last_key.clear();
    };

    for (auto const& entry : unique_entries) {
        std::vector<uint8_t> entry_buf;
        write_le32(entry_buf, static_cast<uint32_t>(entry.key.size()));
        write_le32(entry_buf, static_cast<uint32_t>(entry.value.size()));
        entry_buf.push_back(static_cast<uint8_t>(entry.type));
        entry_buf.insert(entry_buf.end(), entry.key.begin(), entry.key.end());
        entry_buf.insert(entry_buf.end(), entry.value.begin(), entry.value.end());
        if (block_size > 0 && block_size + entry_buf.size() > block_size_limit) {
            flush_block();
        }
        data.insert(data.end(), entry_buf.begin(), entry_buf.end());
        block_last_key = entry.key;
        block_size += static_cast<uint32_t>(entry_buf.size());
    }
    flush_block();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<char*>(data.data()), data.size());
    uint64_t index_offset = static_cast<uint64_t>(data.size());
    std::vector<uint8_t> index_block;
    for (auto const& idx : index) {
        write_le32(index_block, static_cast<uint32_t>(idx.max_key.size()));
        index_block.insert(index_block.end(), idx.max_key.begin(), idx.max_key.end());
        write_le64(index_block, idx.block_offset);
        write_le32(index_block, idx.block_size);
    }
    out.write(reinterpret_cast<char*>(index_block.data()), index_block.size());
    uint64_t bloom_offset = index_offset + index_block.size();
    std::vector<uint8_t> bloom_data = filter.Serialize();
    out.write(reinterpret_cast<char*>(bloom_data.data()), bloom_data.size());

    uint32_t bloom_size = static_cast<uint32_t>(bloom_data.size());
    uint32_t magic = 0x53535430; // SST0
    std::vector<uint8_t> footer;
    write_le64(footer, index_offset);
    write_le64(footer, static_cast<uint64_t>(index_block.size()));
    write_le64(footer, bloom_offset);
    write_le32(footer, bloom_size);
    write_le32(footer, magic);
    out.write(reinterpret_cast<char*>(footer.data()), footer.size());
    return true;
}

std::unique_ptr<SSTable> SSTable::Open(const std::string& path) {
    auto table = std::unique_ptr<SSTable>(new SSTable(path));
    if (!table->Load()) return nullptr;
    return table;
}

bool SSTable::Load() {
    std::ifstream in(path_, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize file_size = in.tellg();
    if (file_size < 32) return false;
    in.seekg(file_size - 32);
    std::vector<uint8_t> footer(32);
    in.read(reinterpret_cast<char*>(footer.data()), 32);
    uint64_t index_offset = read_u64(footer.data());
    uint64_t index_size = read_u64(footer.data() + 8);
    uint64_t bloom_offset = read_u64(footer.data() + 16);
    uint32_t bloom_size = read_u32(footer.data() + 24);
    uint32_t magic = read_u32(footer.data() + 28);
    if (magic != 0x53535430) return false;
    if (index_offset + index_size > static_cast<uint64_t>(file_size - 32)) return false;
    if (bloom_offset + bloom_size > static_cast<uint64_t>(file_size - 32)) return false;

    std::vector<uint8_t> index_block(static_cast<size_t>(index_size));
    in.seekg(index_offset);
    in.read(reinterpret_cast<char*>(index_block.data()), index_block.size());
    size_t pos = 0;
    while (pos < index_block.size()) {
        uint32_t key_len = read_u32(index_block.data() + pos);
        pos += 4;
        if (pos + key_len + 12 > index_block.size()) return false;
        std::string key(reinterpret_cast<char*>(index_block.data() + pos), key_len);
        pos += key_len;
        uint64_t offset = read_u64(index_block.data() + pos);
        pos += 8;
        uint32_t size = read_u32(index_block.data() + pos);
        pos += 4;
        index_.push_back({std::move(key), offset, size});
    }
    std::vector<uint8_t> bloom_data(bloom_size);
    in.seekg(bloom_offset);
    in.read(reinterpret_cast<char*>(bloom_data.data()), bloom_data.size());
    filter_ = BloomFilter::Deserialize(bloom_data);
    data_start_offset_ = 0;
    return true;
}

bool SSTable::ContainsKey(const std::string& key) const {
    if (!filter_.MayContain(key)) return false;
    auto it = std::lower_bound(index_.begin(), index_.end(), key, [](auto const& entry, const std::string& key) {
        return entry.max_key < key;
    });
    if (it == index_.end()) {
        if (index_.empty()) return false;
        it = std::prev(index_.end());
    }
    auto e = FindInBlock(it->block_offset, it->block_size, key);
    return e.has_value() && e->type != WalRecordType::Delete;
}

bool SSTable::Get(const std::string& key, std::string* value) const {
    if (!filter_.MayContain(key)) return false;
    auto it = std::lower_bound(index_.begin(), index_.end(), key, [](auto const& entry, const std::string& key) {
        return entry.max_key < key;
    });
    if (it == index_.end()) {
        if (index_.empty()) return false;
        it = std::prev(index_.end());
    }
    auto entry = FindInBlock(it->block_offset, it->block_size, key);
    if (!entry.has_value()) return false;
    if (entry->type == WalRecordType::Delete) return false;
    if (value) *value = entry->value;
    return true;
}

std::optional<SSTable::Entry> SSTable::FindInBlock(uint64_t offset, uint32_t size, const std::string& key) const {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(offset);
    std::vector<uint8_t> block(size);
    in.read(reinterpret_cast<char*>(block.data()), size);
    size_t pos = 0;
    while (pos + 9 <= block.size()) {
        uint32_t key_len = read_u32(block.data() + pos); pos += 4;
        uint32_t val_len = read_u32(block.data() + pos); pos += 4;
        WalRecordType type = static_cast<WalRecordType>(block[pos++]);
        if (pos + key_len + val_len > block.size()) break;
        std::string k(reinterpret_cast<char*>(block.data() + pos), key_len); pos += key_len;
        std::string v(reinterpret_cast<char*>(block.data() + pos), val_len); pos += val_len;
        if (k == key) return Entry{k, v, type};
    }
    return std::nullopt;
}

} // namespace stratadb
