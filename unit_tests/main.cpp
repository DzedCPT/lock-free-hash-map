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
// Let's seed this to make tests reproducible-ish.
std::mt19937 rng(0);
std::uniform_int_distribution<std::mt19937::result_type> dist(1, 100000);

// To test that this code is thread safe we do the test multiple times
// and with many concurrent threads hoping to ensure all possible
// orderings are covered. So you can increase these values to be more
// confident
const size_t THREAD_INTENSITY =
    25;                          // How many threads to run at the same time.
const size_t REPEATS = 1000; // How many times to repeat each test.

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
                                ConcurrentUnorderedMap<int, int> &cmap) {
    for (const auto &pair : map) {
        cmap.insert({pair.first, pair.second});
    }
}

void deleteMapFromConcurrentMap(const std::unordered_map<int, int> &map,
                                ConcurrentUnorderedMap<int, int> &cmap) {
    for (const auto &pair : map) {
        cmap.erase(pair.first);
    }
}

// Start the test suite with the most basic test checking we can insert and get
// an element. If this test fails then all subsequent tests should also fail.
TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_BasicInsertAndAt) {
    ConcurrentUnorderedMap<int, int> map;
    map.insert({10, 10});
    EXPECT_EQ(map.at(10), 10);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_KVFLoat) {
    ConcurrentUnorderedMap<float, float> map;
    map.insert({10.0, 10.0});
    EXPECT_EQ(map.at(10.0), 10.0);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_KVTypeMismatch) {
    ConcurrentUnorderedMap<int, float> map;
    map.insert({10, 10.0});
    EXPECT_EQ(map.at(10), 10.0);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_VectorKey) {
    ConcurrentUnorderedMap<std::vector<bool>, float> map;
    map.insert({{true, false}, 10.0});
    EXPECT_EQ(map.at({true, false}), 10.0);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_Size) {
    ConcurrentUnorderedMap<int, int> cmap;
    // We don't want to test resize here so make the number of elements here (4)
    // less than the starting capacity of the cmap.
    const auto map = createRandomMap(4);
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap.size(), map.size());
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_SizeWithResize) {
    ConcurrentUnorderedMap<int, int> cmap;
    const auto n = cmap.bucket_count() + (cmap.bucket_count() / 2);
    const auto map = createRandomMap(n);
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap.size(), map.size());
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_Empty) {
    ConcurrentUnorderedMap<int, int> cmap;
    ASSERT_TRUE(cmap.empty());
    cmap.insert({1, 1});
    ASSERT_FALSE(cmap.empty());
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_InsertAndAt1) {
    ConcurrentUnorderedMap<int, int> cmap;
    // We don't want to test resize here so make the number of elements here (4)
    // less than the starting capacity of the cmap.
    const auto map = createRandomMap(4);
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap, map);
}

// TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_InsertAndAt2) {
//     ConcurrentUnorderedMap<int, int> cmap;
//     const auto map = createRandomMap(1);
//     EXPECT_NE(cmap, map);
// }

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_InsertAndAt3) {
    ConcurrentUnorderedMap<int, int> cmap;
    // Fill a map with excactly the number of elements that the underlying cmap
    // can hold to check it handles collisions and wrap around.
    const auto map = createRandomMap(cmap.bucket_count());
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap, map);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_Resize) {
    ConcurrentUnorderedMap<int, int> cmap;
    const auto n = cmap.bucket_count() + (cmap.bucket_count() / 2);
    const auto map = createRandomMap(n);
    insertMapIntoConcurrentMap(map, cmap);
    EXPECT_EQ(cmap, map);
}

TEST(TestConcurrentUnorderedHashMap_SingleThread, Test_Erase) {
    ConcurrentUnorderedMap<int, int> cmap;
    const auto n = cmap.bucket_count() + (cmap.bucket_count() / 2);
    auto map = createRandomMap(n);
    map[10] = 10;
    insertMapIntoConcurrentMap(map, cmap);
    deleteMapFromConcurrentMap(map, cmap);

    map.clear();

    EXPECT_EQ(cmap.size(), 0);
    EXPECT_EQ(cmap, map);
    EXPECT_THROW(cmap.at(10), std::out_of_range);
}

void threadedMapInsert(ConcurrentUnorderedMap<int, int> &cmap,
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
std::vector<std::unordered_map<int, int>>
divideMap(const std::unordered_map<int, int> &originalMap, int n) {
    std::vector<std::unordered_map<int, int>> subMaps(n);
    int totalSize = originalMap.size();
    int subMapSize = totalSize / n;
    int remainder = totalSize % n;

    auto it = originalMap.begin();
    for (int i = 0; i < n; ++i) {
        int currentSubMapSize = subMapSize + (i < remainder ? 1 : 0);
        for (int j = 0; j < currentSubMapSize; ++j) {
            subMaps[i].insert(*it);
            ++it;
        }
    }

    return subMaps;
}

std::unordered_map<int, int>
threadedMapInsertMapPerThread(ConcurrentUnorderedMap<int, int> &cmap,
                              std::unordered_map<int, int> &m,
                              const int nThreads) {
    std::vector<std::unordered_map<int, int>> maps = divideMap(m, nThreads);

    std::vector<std::thread> threads(nThreads);
    for (int i = 0; i < nThreads; i++) {
        threads[i] = std::thread(insertMapIntoConcurrentMap, std::ref(maps[i]),
                                 std::ref(cmap));
    }

    for (auto &t : threads) {
        t.join();
    }
    return m;
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_Size) {
    // We don't want to test resize here so make the number of elements here
    // less than the starting capacity of the cmap.
    ConcurrentUnorderedMap<int, int> cmap;
    const auto map = createRandomMap(4);

    for (int i = 0; i < REPEATS; i++) {
        threadedMapInsert(cmap, map, THREAD_INTENSITY);
        EXPECT_EQ(4, cmap.size());
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_InsertAndAt1) {
    // We don't want to test resize here so make the number of elements here (4)
    // less than the starting capacity of the cmap.
    ConcurrentUnorderedMap<int, int> cmap;
    const auto map = createRandomMap(4);

    for (int i = 0; i < REPEATS; i++) {
        threadedMapInsert(cmap, map, THREAD_INTENSITY);
        EXPECT_EQ(cmap, map);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_Resize) {
    for (int i = 0; i < REPEATS; i++) {
        ConcurrentUnorderedMap<int, int> cmap;
        const auto startingBucketCount = cmap.bucket_count();
        // Make sure we pick a factor here higher than the load factor!
        const auto map = createRandomMap(startingBucketCount * 0.75);

        // Should only resize once, so bucket count should end up beng twice
        // the starting bucket size.
        threadedMapInsert(cmap, map, THREAD_INTENSITY);

        EXPECT_EQ(cmap.bucket_count(), startingBucketCount * 2);
        EXPECT_EQ(cmap, map);
        // assert that the depth is zero, ie that the originally smaller kvs
        // got cleanup up.
        EXPECT_EQ(cmap.depth(), 0);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_DoubleResize) {
    // We don't want to test resize here so make the number of elements here
    // less than the starting capacity of the cmap.

    for (int i = 0; i < REPEATS; i++) {
        ConcurrentUnorderedMap<int, int> cmap;
        const auto startingBucketCount = cmap.bucket_count();
        // The +1 will trigger a second resize with load factor threshold 0.5.
        const auto map = createRandomMap(startingBucketCount + 1);

        threadedMapInsert(cmap, map, THREAD_INTENSITY);
        EXPECT_EQ(cmap.bucket_count(), startingBucketCount * 4);
        EXPECT_EQ(cmap, map);

        // assert that the depth is zero, ie that the originally smaller kvs
        // got cleanup up.
        EXPECT_EQ(cmap.depth(), 0);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_MaxLoadRatio) {
    // We don't want to test resize here so make the number of elements here
    // less than the starting capacity of the cmap.

    // TODO: Under high repeition this test will fail.
    for (int i = 0; i < REPEATS; i++) {
        ConcurrentUnorderedMap<int, int> cmap(5, 1.0);

        const auto startingBucketCount = cmap.bucket_count() * 0.75;
        // Make sure we pick a factor here higher than the load factor!
        // const auto map = createRandomMap(startingBucketCount);
        std::unordered_map<int, int> map{};
        for (int i = 0; i < startingBucketCount; i++) {
            map[i] = i;
        }

        threadedMapInsert(cmap, map, THREAD_INTENSITY);
        // Should only resize once, so bucket count should end up beng twice
        // the starting bucket size.
        EXPECT_EQ(cmap.bucket_count(), 32);
        EXPECT_EQ(cmap.size(), startingBucketCount);
        EXPECT_EQ(cmap, map);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_StragglerInsertOnOldKvs) {
    // This tests the following situation:
    // - Inserter checks if kvs is full and sees that it *isn't*.
    // - While trying to reprobe for an open spot the kvs fills up.
    // In the setting above the inserter would get stuck reprobing the
    // full kvs indefinitely.
    // This test genereates the situation above and makes sure that there's
    // some logic to break out of the insert and either allocate a new kvs
    // yourself or use one another thread has allocated.

    for (int i = 0; i < REPEATS; i++) {
        // Set the resize ratio to 1.0 so that at the point of the resize the
        // current is full, allowing us to check that a thread will detect
        // this, break out of it reprobe loop and use the new larger kvs.
        ConcurrentUnorderedMap<int, int> cmap(5, 1.0);

        const auto startingBucketCount = cmap.bucket_count();
        const auto map = createRandomMap(startingBucketCount + 10);

        threadedMapInsert(cmap, map, THREAD_INTENSITY);
        EXPECT_EQ(cmap.bucket_count(), startingBucketCount * 2);
        EXPECT_EQ(cmap, map);
        // assert that the depth is zero, ie that the originally smaller kvs
        // got cleanup up.
        EXPECT_EQ(cmap.depth(), 0);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread,
     Test_UniqueValueInsertedByEachThread_WithResize) {
    // In this test we get each thread to insert unique values.
    // This was causing a data race where the copy was colliding
    // with inserts into the old table.

    for (int i = 0; i < REPEATS; i++) {
        ConcurrentUnorderedMap<int, int> cmap(7, 0.3);
        auto m = createRandomMap(16 * 16);
        auto map = threadedMapInsertMapPerThread(cmap, m, 16);
        EXPECT_EQ(cmap, map);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread,
     Test_CopyDoesnNotOverrideNewValues) {
    // In this test we are testing the case that every value is replaced after a
    // resize starts. We need to assert that we don't override the new values
    // with the old values during the copy, because some copies will happen
    // after the new values are inserted and we need to detect that and drop the
    // copy.

    for (int i = 0; i < REPEATS; i++) {
        // cmap will have 2**9=512 slots to start:
        ConcurrentUnorderedMap<int, int> cmap(9, 0.5);
        // Insert 256 values which is exactly 1 short of triggering a resize.
        auto m = createRandomMap(256);
        // Insert the values across 16 threads:
        threadedMapInsertMapPerThread(cmap, m, 16);
        // Assert the that indeed no resize has been trigger!
        EXPECT_EQ(cmap.depth(), 0);

        // Pick 0 because we can be sure that's a new key!
        // The insert below should trigger a resize.
        cmap.insert({0, 0});
        // Assert resize has began.
        EXPECT_EQ(cmap.depth(), 1);

        // Give each pair a new negative value, so we can check at the end if
        // the value in cmap at the end is the new (expected) or old value.
        int idx = 0;
        for (auto &pair : m) {
            m[pair.first] = idx;
            idx--;
        }
        threadedMapInsertMapPerThread(cmap, m, 16);

        // Need to insert this into map because we inserted it into cmap to
        // trigger the resize.
        m[0] = 0;

        // Check that no old values from before the copy still exist in the map.
        EXPECT_EQ(cmap, m);
        // Check that the copy is complete.
        EXPECT_EQ(cmap.depth(), 0);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_Erase) {
    for (int i = 0; i < REPEATS; i++) {
        // We don't want to test resize here so make the number of elements here
        // (4)
        // less than the starting capacity of the cmap.
        ConcurrentUnorderedMap<int, int> cmap;
        auto map = createRandomMap(4);
        map[10] = 10;

        threadedMapInsert(cmap, map, THREAD_INTENSITY);
        deleteMapFromConcurrentMap(map, cmap);
        map.clear();

        EXPECT_EQ(cmap, map);
        EXPECT_EQ(cmap.size(), 0);
        EXPECT_THROW(cmap.at(10), std::out_of_range);
    }
}

TEST(TestConcurrentUnorderedHashMap_MultiThread, Test_EraseDuringResize) {

    for (int i = 0; i < REPEATS; i++) {
        ConcurrentUnorderedMap<int, int> cmap(9, 0.5);
        auto map = createRandomMap(256);
        threadedMapInsertMapPerThread(cmap, map, 16);
        // Assert the that indeed no resize has been trigger!
        EXPECT_EQ(cmap.depth(), 0);

        // Pick 0 because we can be sure that's a new key!
        // The insert below should trigger a resize.
        cmap.insert({0, 0});
        map[0] = 0;
        // Assert resize has began.
        EXPECT_EQ(cmap.depth(), 1);

        deleteMapFromConcurrentMap(map, cmap);
        map.clear();

        EXPECT_EQ(cmap, map);
        EXPECT_EQ(cmap.size(), 0);
        EXPECT_EQ(cmap.depth(), 1);
        EXPECT_THROW(cmap.at(0), std::out_of_range);
    }
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
