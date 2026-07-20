#include "stratadb/skip_list.h"
#include <thread>
#include <vector>
#include <iostream>
#include <cassert>

using namespace stratadb;

int main() {
    SkipList sl(12);
    const int N = 1000;
    const int T = 8;

    // concurrent inserts
    std::vector<std::thread> writers;
    for (int t = 0; t < T; ++t) {
        writers.emplace_back([t, &sl]() {
            for (int i = 0; i < N; ++i) {
                int k = t * N + i;
                sl.Insert(std::to_string(k), "v" + std::to_string(k));
            }
        });
    }
    for (auto& th : writers) th.join();

    // readers
    std::vector<std::thread> readers;
    std::atomic<int> found{0};
    for (int t = 0; t < T; ++t) {
        readers.emplace_back([t, &sl, &found]() {
            for (int i = 0; i < 100; ++i) {
                int k = (t * 100 + i);
                std::string v;
                if (sl.Get(std::to_string(k), &v)) ++found;
            }
        });
    }
    for (auto& th : readers) th.join();

    size_t expected = static_cast<size_t>(N) * T;
    if (sl.Size() != expected) {
        std::cerr << "Unexpected size: " << sl.Size() << " expected " << expected << "\n";
        return 2;
    }

    // spot-check some values
    for (int k = 0; k < 10; ++k) {
        std::string v;
        bool ok = sl.Get(std::to_string(k), &v);
        assert(ok);
        assert(v == ("v" + std::to_string(k)));
    }

    std::cout << "skiplist test passed\n";
    return 0;
}
