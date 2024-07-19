
#include "gtest/gtest.h"
#include <thread>

template <typename T>
class Data {

   public:
    std::atomic<int> mRef = 0;
    T* mData;
    Data(T* ptr) : mData(ptr), mRef(1) {}
    ~Data() { delete mData; }
};

template <typename T>
class SmartPointer {
    // ZZZ: SHould make some of this private.
   public:
    // This is the thing being shared!!
    std::atomic<Data<T>*> mPtr = nullptr;
    // Ptr<T>* oldPtr;

   public:
    // Constructor
    SmartPointer(T* p = nullptr) : mPtr(new Data(p)) {}
    SmartPointer(SmartPointer<T> const& other) {
        other.mPtr.load()->mRef++;
        mPtr.store(other.mPtr.load());
    }
	SmartPointer& operator=(const SmartPointer& other) {
        // Copy assignment implementation
        return *this;
    }

    // Destructor
    ~SmartPointer() {
        while (true) {
            auto data = mPtr.load();
            auto refCount = data->mRef.load();
            assert(refCount >= 1);
            if (data->mRef.compare_exchange_strong(refCount, refCount - 1)) {
                // Check if it's my job to delete this!
                if (refCount == 1) {
                    delete mPtr;
                }
                break;
            }
        }
        // delete mPtr;
    }

    // Overload the -> operator
    T* operator->() { return mPtr; }

    // Overload the * operator
    T& operator*() { return *mPtr; }

    bool compare_exchange_strong(SmartPointer<T>& expected,
                                 SmartPointer<T> desired) {
        // I need to make a copy here:
        auto x = new SmartPointer<T>(desired);
        auto y = expected.mPtr.load();
        if (mPtr.compare_exchange_strong(y, x->mPtr.load())) {
            // expected.mPtr.load()->mRef--;
            return true;
        }
        // delete x;
        return false;
    }
};

// TEST(Test, Test_CompareExchangeAvoidSegfault) {
//     SmartPointer<int> x(new int(10));
// 	// Copy the pointer to make sure things still work.
//     auto y = x;
//     // SmartPointer<int> y(new int(20));
//     // auto x = new int(10)/* ; */
//     // p.compare_exchange(p, y);
//     // EXPECT_EQ(p.mPtr, y);
// }

// TEST(Test, Test_Something) {
//     SmartPointer<int> p(new int(10));
//     SmartPointer<int> y(new int(20));
//     p.compare_exchange_strong(p, y);
//     // EXPECT_EQ(p.mPtr, y);
// }
//

struct TestStruct {
    int x = 10;
};

void func(SmartPointer<TestStruct> ptr, int i) {
    if (i == 0) {
        SmartPointer<TestStruct> x(new TestStruct{20});
        while (!ptr.compare_exchange_strong(ptr, x)) {
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20 * i));
}

TEST(Test, Test_Something) {
    int nThreads = 10;
    SmartPointer<TestStruct> p(new TestStruct());
    std::vector<std::thread> threads(nThreads);

    for (int i = 0; i < nThreads; i++) {
        threads[i] = std::thread(func, p, i);
    }

    for (auto& t : threads) {
        t.join();
    }
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
