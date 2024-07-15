#include "map.h"
#include "gtest/gtest.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

std::random_device dev;
// Let's not set this to make test reproducible-ish.
// std::mt19937 rng(dev());
std::mt19937 rng(0);
std::uniform_int_distribution<std::mt19937::result_type> dist(1, 100000);

std::unordered_map<int, int> createRandomMap(const int n) {
    std::unordered_map<int, int> map;
    for (int i = 0; i < n; i++) {
        const auto key = dist(rng);
        const auto value = dist(rng);
        map.insert({key, value});
    }
    return map;
}

void insertMapIntoConcurrentMap(const std::unordered_map<int, int> &map,
                                ConcurrentUnorderedMap &cmap) {
    for (const auto &pair : map) {
        cmap.insert({pair.first, pair.second});
    }
}

// Start the test suite with the most basic test checking we can insert and get
// an element. If this test fails then all subsequent tests should also fail.
TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_BasicInsertAndAt) {
    ConcurrentUnorderedMap map;
    map.insert({10, 10});
    EXPECT_EQ(map.at(10), 10);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_Size) {
    ConcurrentUnorderedMap cmap;
    // We don't want to test resize here so make the number of elements here (4)
    // less than the starting capacity of the cmap.
    const auto map = createRandomMap(4);
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap.size(), map.size());
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_Empty) {
    ConcurrentUnorderedMap cmap;
    ASSERT_TRUE(cmap.empty());
    cmap.insert({1, 1});
    ASSERT_FALSE(cmap.empty());
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_InsertAndAt1) {
    ConcurrentUnorderedMap cmap;
    // We don't want to test resize here so make the number of elements here (4)
    // less than the starting capacity of the cmap.
    const auto map = createRandomMap(4);
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap, map);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_InsertAndAt2) {
    ConcurrentUnorderedMap cmap;
    const auto map = createRandomMap(1);
    EXPECT_NE(cmap, map);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_InsertAndAt3) {
    ConcurrentUnorderedMap cmap;
	// Fill a map with excactly the number of elements that the underlying cmap can hold
	// to check it handles collisions and wrap around.
    const auto map = createRandomMap(cmap.bucket_count());
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap, map);
}


int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
