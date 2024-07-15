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
    while (true) {
        const auto key = dist(rng);
        if (map.find(key) != map.end()) {
            continue;
        }
        const auto value = dist(rng);
        map.insert({key, value});
        if (map.size() == n) {
            return map;
        }
    }
    assert(false);
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

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_SizeWithResize) {
    ConcurrentUnorderedMap cmap;
    const auto n = cmap.bucket_count() + (cmap.bucket_count() / 2);
    const auto map = createRandomMap(n);
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
    // Fill a map with excactly the number of elements that the underlying cmap
    // can hold to check it handles collisions and wrap around.
    const auto map = createRandomMap(cmap.bucket_count());
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap, map);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_Resize) {
    ConcurrentUnorderedMap cmap;
    const auto n = cmap.bucket_count() + (cmap.bucket_count() / 2);
    const auto map = createRandomMap(n);
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap, map);
}

void threadedMapInsert(ConcurrentUnorderedMap &cmap,
                       const std::unordered_map<int, int> &map,
                       const int nThreads) {
    std::vector<std::thread> threads(nThreads);

    for (int i = 0; i < nThreads; i++) {
        threads[i] = std::thread(insertMapIntoConcurrentMap, std::ref(map),
                                 std::ref(cmap));
    }

    for (auto &t : threads) {
        t.join();
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_Size) {
    // We don't want to test resize here so make the number of elements here (4)
    // less than the starting capacity of the cmap.
    ConcurrentUnorderedMap cmap;
    const auto map = createRandomMap(4);

    for (int i = 0; i < 100; i++) {
        threadedMapInsert(cmap, map, 10);
        EXPECT_EQ(4, cmap.size());
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_InsertAndAt1) {
    // We don't want to test resize here so make the number of elements here (4)
    // less than the starting capacity of the cmap.
    ConcurrentUnorderedMap cmap;
    const auto map = createRandomMap(4);

    for (int i = 0; i < 100; i++) {
        threadedMapInsert(cmap, map, 10);
        EXPECT_EQ(cmap, map);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_Resize) {
    // We don't want to test resize here so make the number of elements here
    // less than the starting capacity of the cmap.
    ConcurrentUnorderedMap cmap;
    const auto startingBucketCount = cmap.bucket_count();
    // Make sure we pick a factor here higher than the load factor!
    const auto map = createRandomMap(startingBucketCount * 0.75);

	// TODO: I know that if I increase this it will break on the cmap.bucket_count()
	// function call
    for (int i = 0; i < 100; i++) {
        threadedMapInsert(cmap, map, 2);
        // Should only resize once, so bucket count should end up beng twice
        // the starting
        // bucket size.
        EXPECT_EQ(cmap.bucket_count(), startingBucketCount * 2);
		EXPECT_EQ(cmap, map);
		// TODO: Next step, assert that the map cleans up earlier
		// version
		// EXPECT_EQ(cmap.depth(), 1);
    }
}

// TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_DoubleResize) {
//     // We don't want to test resize here so make the number of elements here
//     // less than the starting capacity of the cmap.
//     ConcurrentUnorderedMap cmap;
//
//     const auto startingBucketCount = cmap.bucket_count();
// 	std::cout  << startingBucketCount << std::endl;
// 	// The +1 will trigger a second resize with load factor threshold 0.5.
//     const auto map = createRandomMap(startingBucketCount + 1);
//
//     for (int i = 0; i < 1; i++) {
//         threadedMapInsert(cmap, map, 2);
//         EXPECT_EQ(cmap.bucket_count(), startingBucketCount * 4);
// 		EXPECT_EQ(cmap, map);
//     }
// }

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
