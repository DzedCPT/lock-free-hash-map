#include "map.h"
#include "gtest/gtest.h"
#include <iostream>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

void singleThreadInsert(Map &map, const int nElements) {
    for (int i = 0; i < nElements; i++) {
        map.insert(i, i);
    }
}

void multiThreadInsert(Map &map, const int nThreads, const int nElements) {

    std::vector<std::thread> threads(nThreads);

    for (int i = 0; i < nThreads; i++) {
        threads[i] = std::thread(singleThreadInsert, std::ref(map), nElements);
    }

    for (auto &t : threads) {
        t.join();
    }
}

TEST(TestConcurrentHashMap, TestSizeSingleThread) {
    Map map{};
    multiThreadInsert(map, 1, 10);
    EXPECT_EQ(map.size(), 10);
}

TEST(TestConcurrentHashMap, TestSingleThreadInsert) {
    for (int i = 0; i < 1; i++) {
        Map map{};
        multiThreadInsert(map, 1, 10);

        for (int i = 0; i < 10; i++) {
            EXPECT_EQ(map.get(i), i);
        }
    }
}

TEST(TestConcurrentHashMap, TestMultiThreadInsert) {
    for (int i = 0; i < 10000; i++) {
        Map map{};
        multiThreadInsert(map, 100, 10);

        for (int i = 0; i < 10; i++) {
            EXPECT_EQ(map.get(i), i);
        }
    }
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
