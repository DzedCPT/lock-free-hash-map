#include <iostream>
#include <thread>
#include <tuple>
#include <vector>
#include <unordered_map>
#include "gtest/gtest.h"

void singleThreadInsert(std::unordered_map<int, int> &map, const int nElements) {
    for (int i = 0; i < nElements; i++) {
        map.insert({i, i});
    }
}

void multiThreadInsert(std::unordered_map<int, int>& map, const int nThreads, const int nElements) {
    std::vector<std::thread> threads;

    for (int i = 0; i < nThreads; i++) {
        threads.push_back(std::thread(singleThreadInsert, std::ref(map), nElements));
    }

    for (auto &t : threads) {
        t.join();
    }

}


TEST(TestConcurrentHashMap, TestSize) {
	std::unordered_map<int, int> map(20);
	multiThreadInsert(map, 2, 10);
  	EXPECT_EQ(map.size(), 20);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
