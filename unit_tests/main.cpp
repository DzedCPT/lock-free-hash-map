#include "map.h"
#include "gtest/gtest.h"
#include <iostream>
#include <random>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

std::random_device dev;
// std::mt19937 rng(dev());
std::mt19937 rng(0);
std::uniform_int_distribution<std::mt19937::result_type>
    dist6(1, 100000); // distribution in range [1, 6]

std::vector<std::tuple<int, int>> randomVector(int n) {
    std::vector<std::tuple<int, int>> values;
    std::vector<int> intVector(10);
    std::unordered_set<int> intSet;
    while (true) {
        auto key = dist6(rng);
        // Make sure each key is unique.
        if (intSet.find(key) != intSet.end()) {
            continue;
        }
        values.push_back({key, dist6(rng)});
        intSet.insert(key);
        if (values.size() == n) {
            break;
        }
    }
    return values;
}

std::vector<std::tuple<int, int>> seqVector(int n) {
    std::vector<std::tuple<int, int>> values;
    for (int i = 0; i < n; i++) {
        values.push_back({i, i});
    }
    return values;
}

void singleThreadInsert(Map &map, const int nElements,
                        const std::vector<std::tuple<int, int>> &values) {
    for (const auto &pair : values) {
        map.insert(std::get<0>(pair), std::get<1>(pair));
    }
}

void multiThreadInsert(Map &map, const int nThreads, const int nElements,
                       const std::vector<std::tuple<int, int>> &values) {
    std::vector<std::thread> threads(nThreads);

    for (int i = 0; i < nThreads; i++) {
        threads[i] =
            std::thread(singleThreadInsert, std::ref(map), nElements, values);
    }

    for (auto &t : threads) {
        t.join();
    }
}

TEST(TestConcurrentHashMap, TestSizeSingleThread) {
    Map map{};
    const auto values = seqVector(10);
    multiThreadInsert(map, 1, 10, values);
    EXPECT_EQ(map.size(), 10);
}

TEST(TestConcurrentHashMap, TestSingleThreadInsert) {
    const auto values = seqVector(10);
    for (int i = 0; i < 1; i++) {
        Map map{};
        multiThreadInsert(map, 1, 10, values);

        for (int i = 0; i < 10; i++) {
            EXPECT_EQ(map.get(i), i);
        }
    }
}

TEST(TestConcurrentHashMap, TestMultiThreadInsert) {
    const auto values = seqVector(10);

    for (int i = 0; i < 100; i++) {
        Map map(2000);
        multiThreadInsert(map, 100, 10, values);

        for (int i = 0; i < 10; i++) {
            EXPECT_EQ(map.get(i), i);
        }
    }
}

TEST(TestConcurrentHashMap, TestCollisionResolutionSingleThread) {
    // Does it make sense to run this test multiple times.
    int cap = 10;
    Map map(cap);
    const auto values = randomVector(cap);
    for (int i = 0; i < 1; i++) {
        multiThreadInsert(map, 1, 10, values);

        for (const auto &pair : values) {
            EXPECT_EQ(map.get(std::get<0>(pair)), std::get<1>(pair));
        }
    }
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
