
#ifndef WRAPPER_H
#define WRAPPER_H

#include <atomic>
#include <utility>
#include "kvs.h"

template <typename K, typename V>
class AtomicKvsPtr {
public:
    AtomicKvsPtr(KeyValueStore<K, V>* kvs): mKvs(kvs) {}

	KeyValueStore<K, V>* load() {
		return mKvs;
	}

	KeyValueStore<K, V>* constLoad() const {
		return mKvs;
	}

	bool exchange(KeyValueStore<K, V>* expected, KeyValueStore<K, V>* desired) {
		return mKvs.compare_exchange_strong(expected, desired);
	}

    std::atomic<KeyValueStore<K, V>*> mKvs;
    std::atomic<int> mRefCount;
};


#endif  // WRAPPER_H



