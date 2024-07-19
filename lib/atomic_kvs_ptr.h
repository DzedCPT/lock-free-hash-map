#include <atomic>

#ifndef WRAPPER_H
#define WRAPPER_H


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
	bool isNull()const {
		return mPtr.load() == nullptr;
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
    T* operator->() const { return mPtr.load()->mData; }



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


// #include <atomic>
// #include <utility>
// #include "kvs.h"
//
//
// template <typename K, typename V>
// class AtomicKvsPtr;
//
// template<typename K, typename V>
// class Ptr {
// public:
//     explicit Ptr(AtomicKvsPtr<K, V>* kvs, KeyValueStore<K, V>*);
//     ~Ptr();
//     KeyValueStore<K, V>* ref();
//     KeyValueStore<K, V>* mKvs;
//     AtomicKvsPtr<K, V>* mAtomicPtr;
// };
//
// template <typename K, typename V>
// class AtomicKvsPtr {
// public:
//     explicit AtomicKvsPtr(KeyValueStore<K, V>* kvs): mKvs(kvs) {}
//
// 	Ptr<K, V> load() {
// 		return Ptr<K, V>(this, mKvs);
// 	}
//
//         // ZZZ: Maybe a better name for this would be const load?
// 	KeyValueStore<K, V>* constLoad() const {
// 		return mKvs;
// 	}
//
// 	bool exchange(KeyValueStore<K, V>* expected, KeyValueStore<K, V>* desired) {
// 		return mKvs.compare_exchange_strong(expected, desired);
// 	}
//
//     std::atomic<KeyValueStore<K, V>*> mKvs;
//     std::atomic<int> mRefCount{};
// };
//
// template <typename K, typename V>
// Ptr<K, V>::Ptr(AtomicKvsPtr<K, V>* kvs, KeyValueStore<K, V>* kvs2) : mKvs(kvs2), mAtomicPtr(kvs) {}
//
// template <typename K, typename V>
// Ptr<K, V>::~Ptr() {
//     // --mKvs->mRefCount;
// }
//
// template <typename K, typename V>
// KeyValueStore<K, V>* Ptr<K, V>::ref() {
//     return mKvs;
// }




#endif  // WRAPPER_H



