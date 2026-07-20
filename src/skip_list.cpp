#include "stratadb/skip_list.h"
#include <algorithm>

namespace stratadb {

SkipList::SkipList(int max_level)
    : max_level_(max_level), cur_level_(1), rng_(std::random_device{}()), dist_(0, 1), size_(0) {
    head_ = new Node(max_level_);
}

SkipList::~SkipList() {
    Node* n = head_;
    while (n) {
        Node* next = n->next[0];
        delete n;
        n = next;
    }
}

int SkipList::RandomLevel() {
    int lvl = 1;
    while (lvl < max_level_ && (rng_() & 1)) ++lvl;
    return lvl;
}

bool SkipList::Insert(const std::string& key, const std::string& value, WalRecordType type) {
    std::unique_lock lock(mu_);
    std::vector<Node*> update(max_level_, nullptr);
    Node* x = head_;
    for (int i = cur_level_ - 1; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];
        update[i] = x;
    }
    x = x->next[0];
    if (x && x->key == key) {
        x->value = value;
        x->type = type;
        return false;
    } else {
        int lvl = RandomLevel();
        if (lvl > cur_level_) {
            for (int i = cur_level_; i < lvl; ++i) update[i] = head_;
            cur_level_ = lvl;
        }
        Node* n = new Node(lvl);
        n->key = key;
        n->value = value;
        n->type = type;
        for (int i = 0; i < lvl; ++i) {
            n->next[i] = update[i]->next[i];
            update[i]->next[i] = n;
        }
        ++size_;
        return true;
    }
}

bool SkipList::Get(const std::string& key, std::string* value, WalRecordType* type) const {
    std::shared_lock lock(mu_);
    Node* x = head_;
    for (int i = cur_level_ - 1; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];
    }
    x = x->next[0];
    if (x && x->key == key) {
        if (value) *value = x->value;
        if (type) *type = x->type;
        return true;
    }
    return false;
}

void SkipList::ForEach(std::function<void(const std::string&, const std::string&, WalRecordType)> cb) const {
    std::shared_lock lock(mu_);
    Node* x = head_->next[0];
    while (x) {
        cb(x->key, x->value, x->type);
        x = x->next[0];
    }
}

size_t SkipList::Size() const {
    std::shared_lock lock(mu_);
    return size_;
}

} // namespace stratadb
