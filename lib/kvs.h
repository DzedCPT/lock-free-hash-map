#include "consts.h"
#include "slot.h"
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#ifndef KVS_H
#define KVS_H

template <typename K, typename V>
class KeyValueStore {
   public:
    KeyValueStore(size_t size, float maxLoadRatio);

    size_t size() const;

    bool empty() const;

    size_t bucket_count() const;

    // TODO: According to the spec this should return: pair<iterator,bool>
    // insert ( const value_type& val );
    V insert(std::pair<K, V> const& val);

    // TODO: According to the spec this should return: size_t
    void erase(K key);

    V atKvs(K key);

    KeyValueStore* nextKvs() const;

    bool copied() const;

    bool hasActiveReaders() const;

    V at(K const key);

   private:
    size_t hash(K const key) const;

    void newKvs();

    size_t getCopyBatchIdx();

    void copySlot(size_t idx);

    void copyBatch();

    Slot<K, V>* insertKey(K const key);

    V insertValue(Slot<K, V>* slot, V value, DataState valueState);

    V insert(std::pair<K, V> const& val, DataState const valueState);

    bool eraseKvs(K const key);

    V insertKvs(std::pair<K, V> const& val, DataState const valueState);

    bool resizeRequired() const;

    size_t clip(size_t const slot) const;

    std::atomic<size_t> mSize{};
    std::vector<Slot<K, V>> mKvs;
    std::atomic<KeyValueStore*> mNextKvs = nullptr;
    std::atomic<size_t> mCopyIdx;
    std::atomic<size_t> mNumReaders = 0;
    // mCopied doesn't need to be atomic because it's only every going to change
    // from false to true. and it doesn't matter how many times that happens.
    bool mCopied = false;
    float const mMaxLoadRatio;
    std::hash<K> mHash;
};

#endif  // KVS_H
