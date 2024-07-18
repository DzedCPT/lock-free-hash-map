
#ifndef WRAPPER_H
#define WRAPPER_H

#include <atomic>
#include <utility>
#include "kvs.h"


template <typename K, typename V>
class AtomicKvsPtr;

template<typename K, typename V>
class Ptr {
public:
    explicit Ptr(AtomicKvsPtr<K, V>* kvs, KeyValueStore<K, V>*);
    ~Ptr();
    KeyValueStore<K, V>* ref();
    KeyValueStore<K, V>* mKvs;
    AtomicKvsPtr<K, V>* mAtomicPtr;
};

template <typename K, typename V>
class AtomicKvsPtr {
public:
    explicit AtomicKvsPtr(KeyValueStore<K, V>* kvs): mKvs(kvs) {}

	Ptr<K, V> load() {
		return Ptr<K, V>(this, mKvs);
	}

        // ZZZ: Maybe a better name for this would be const load?
	KeyValueStore<K, V>* constLoad() const {
		return mKvs;
	}

	bool exchange(KeyValueStore<K, V>* expected, KeyValueStore<K, V>* desired) {
		return mKvs.compare_exchange_strong(expected, desired);
	}

    std::atomic<KeyValueStore<K, V>*> mKvs;
    std::atomic<int> mRefCount{};
};

template <typename K, typename V>
Ptr<K, V>::Ptr(AtomicKvsPtr<K, V>* kvs, KeyValueStore<K, V>* kvs2) : mKvs(kvs2), mAtomicPtr(kvs) {}

template <typename K, typename V>
Ptr<K, V>::~Ptr() {
    // --mKvs->mRefCount;
}

template <typename K, typename V>
KeyValueStore<K, V>* Ptr<K, V>::ref() {
    return mKvs;
}




#endif  // WRAPPER_H



