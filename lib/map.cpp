#include "map.h"
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace cmap {

typedef std::size_t size_t;

enum DataState {
    EMPTY,         // The data has not been set
    ALIVE,         // The data is to be used.
    TOMB_STONE,    // The data has been removed.
    COPIED_DEAD,   // The data has been copied from the current location
    COPIED_ALIVE,  // The copy has been copied into the current location.
};

template <typename T>
class DataWrapper {
   public:
    DataWrapper(T value, DataState state) : mData(value), mState(state) {}

    bool empty() const {
        return !(mState == ALIVE || mState == COPIED_DEAD ||
                 mState == COPIED_ALIVE);
    }
    bool fromPrevKvs() const { return mState == COPIED_ALIVE; }
    bool dead() const { return mState == COPIED_DEAD || mState == TOMB_STONE; }
    bool eval(T val) const {
        if (mState == ALIVE || mState == COPIED_ALIVE) return val == mData;
        return false;
    }

    // getters
    T data() const { return mData; }
    DataState state() const { return mState; }

   private:
    T const mData;
    DataState mState = EMPTY;
};

template <typename K, typename V>
class Slot {
   public:
    Slot() {
        mKey.store(new DataWrapper<K>(K(), EMPTY));
        mValue.store(new DataWrapper<V>(V(), EMPTY));
    }

    ~Slot() {
        delete mKey.load();
        delete mValue.load();
    }

    bool casValue(DataWrapper<V> const* expected,
                  DataWrapper<V> const* desired) {
        auto const success = mValue.compare_exchange_strong(expected, desired);
        if (success) delete expected;
        return success;
    }

    bool casKey(DataWrapper<K> const* expected, DataWrapper<K> const* desired) {
        bool const success = mKey.compare_exchange_strong(expected, desired);
        if (success) delete expected;
        return success;
    }

    DataWrapper<K> const* key() const { return mKey.load(); }

    DataWrapper<V> const* value() const { return mValue.load(); }

   private:
    std::atomic<DataWrapper<K> const*> mKey{};
    std::atomic<DataWrapper<V> const*> mValue{};
};

template <typename K, typename V>
class KeyValueStore {
   public:
    KeyValueStore(size_t size, float maxLoadRatio)
        : mKvs(std::vector<Slot<K, V>>(size)), mMaxLoadRatio(maxLoadRatio) {}

    size_t size() const {
        size_t s = mSize;
        if (mNextKvs != nullptr) {
            s += nextKvs()->size();
        }
        return s;
    }

    bool empty() const {
        auto empty = mSize == 0;
        auto next_empty = true;
        if (mNextKvs != nullptr) {
            next_empty = nextKvs()->empty();
        }
        return empty && next_empty;
    }

    size_t bucket_count() const {
        if (mNextKvs != nullptr) {
            return nextKvs()->bucket_count();
        }
        return mKvs.size();
    }

    // TODO: According to the spec this should return: pair<iterator,bool>
    // insert ( const value_type& val );
    V insert(std::pair<K, V> const& val) { return insert(val, ALIVE); }

    // TODO: According to the spec this should return: size_t
    void erase(K const key) {
        if (eraseKvs(key)) {
            mSize--;
            return;
        }
        if (mNextKvs.load() != nullptr) mNextKvs.load()->erase(key);
    }

    V atKvs(K const key) {
        // mNumReaders++;
        int slot = hash(key);
        while (true) {
            auto const& d = mKvs[slot];
            auto const currentKeyValue = d.key();
            if (currentKeyValue->eval(key)) {
                auto value = d.value();
                if (value->dead()) {
                    if (mNextKvs == nullptr) {
                        mNumReaders--;
                        throw std::out_of_range("Unable to find key");
                    } else {
                        return nextKvs()->at(key);
                    }
                }
                // If value is empty, we're tyring to read from a slot that's
                // only partially set ie the key is set but not yet the value.
                // So we need to start again until we can see the value or it's
                // killed.
                if (value->empty()) continue;
				// Value found, let's return.
                return value->data();
            }
            if (currentKeyValue->empty() || currentKeyValue->dead()) {
                if (mNextKvs == nullptr) {
                    throw std::out_of_range("Unable to find key");
                } else {
                    auto result = nextKvs()->at(key);
                    return result;
                }
            }
            slot = clip(slot + 1);
        }
        assert(false);
    }

    V at(K const key) {
        if (mCopied) {
            // Not possible to be copied and not have a nextKvs, because
            // otherwise where did we copy everything into.
            assert(mNextKvs != nullptr);
            return nextKvs()->at(key);
        }

        mNumReaders++;
        auto const result = atKvs(key);
        mNumReaders--;
        return result;
    }

    KeyValueStore* nextKvs() const { return mNextKvs.load(); }

    bool copied() const { return mCopied; }

    bool hasActiveReaders() const { return mNumReaders != 0; }

   private:
    size_t hash(K const key) const { return clip(mHash(key)); }

    void newKvs() {
        // You could check here if anybody else has already started a resize and
        // if so not allocate memory.

        auto* ptr = new KeyValueStore(mKvs.size() * 2, mMaxLoadRatio);
        KeyValueStore<K, V>* null_lvalue = nullptr;
        // Only thread should win the race and put the newKvs into place.
        if (!mNextKvs.compare_exchange_strong(null_lvalue, ptr)) {
            // Allocated for nothing, some other thread beat us,
            // so cleanup our mess.
            delete ptr;
        }
    }

    size_t getCopyBatchIdx() {
        auto startIdx = mCopyIdx.load();
        if (startIdx >= mKvs.size()) {
            return mKvs.size();
        }

        size_t endIdx = startIdx + COPY_CHUNK_SIZE;
        if (!mCopyIdx.compare_exchange_strong(startIdx, endIdx)) {
            // Another thread claimed this work before us.
            return mKvs.size();
        }
        return startIdx;
    }

    void copySlot(size_t idx) {
        DataWrapper<K>* keyCopiedMarker = new DataWrapper<K>(K(), COPIED_DEAD);

        Slot<K, V>* slot = &mKvs[idx];
        auto key = slot->key();
        assert(!key->dead());

        // Let's see if we can put a COPIED state into an EMPTY key:
        if (key->empty()) {
            if (slot->casKey(key, keyCopiedMarker)) return;
            // Key was EMPTY when we last checked, but not by the time the
            // cas was attempted so we need to copy the value into the new
            // kvs.
        }

        delete keyCopiedMarker;
        DataWrapper<V>* valueCopiedMarker =
            new DataWrapper<V>(V(), COPIED_DEAD);

        // key wasn't EMPTY so we need to forward the value into the new table.
        while (true) {
            auto value = slot->value();
            auto data = value->data();

            // Some assertions for my sanity.
            assert(!slot->key()->empty());
            assert(!slot->key()->dead());
            assert(value->state() != COPIED_DEAD);
            assert(mNextKvs != nullptr);

            if (value->state() == TOMB_STONE) {
                delete valueCopiedMarker;
                return;
            }

            if (value->empty()) {
                // We got here so the key wasn't empty, but the value is empty.
                // This means we've got inbetweeen an insert that had inserted
                // the key but not yet the value.
                // So we need to wait until the value is visible before we can
                // do the cas below.
                continue;
            }

            if (slot->casValue(value, valueCopiedMarker)) {
                nextKvs()->insert({key->data(), data}, COPIED_ALIVE);
                mSize--;
                return;
            }
        }
        assert(false);
    }

    void copyBatch() {
        auto const startIdx = getCopyBatchIdx();
        if (startIdx == mKvs.size()) {
            // Either the copy is done or another thread got the work.
            return;
        }

        size_t const endIdx = startIdx + COPY_CHUNK_SIZE;

        for (auto i = startIdx; i < endIdx; i++) copySlot(i);

        if (endIdx == mKvs.size()) mCopied = true;

        assert(endIdx <= mKvs.size());
    }

    Slot<K, V>* insertKey(K const key) {
        DataWrapper<K> const* desiredKey = new DataWrapper<K>(key, ALIVE);
        int idx = hash(desiredKey->data());
        auto* slot = &mKvs[idx];

        while (true) {
            DataWrapper<K> const* currentKey = slot->key();
            // Check if we've fo und an open space:
            if (currentKey->empty()) {
                // Not 100% sure what the difference is here between _strong and
                // _weak:
                // https://stackoverflow.com/questions/4944771/stdatomic-compare-exchange-weak-vs-compare-exchange-strong

                if (slot->casKey(currentKey, desiredKey)) {
                    // yay!! We inserted the key.
                    ++mSize;
                    break;
                }
                // We saw an empty key but failed to CAS our key in.
                // - Either another thread CAS'd its key in before us.
                // - Or an earlier CAS is not yet visible to this thread.
                // Either way we don't have up-to-date information on what
                // key is stored in the current slot, meaning we need to
                // start again.
                continue;
            }

            if (currentKey->eval(desiredKey->data())) {
                // The current key has the same value as the one were trying to
                // insert. So we can just use the current key but need to not
                // leak the memory of the newly allocated key.
                delete desiredKey;
                break;
            }

            // So we failed to claim a key slot:
            // If a resize is required let's not bother continueing to insert
            // into this kvs, and instead check if we can insert into the new
            // resized Kvs. NOTE: Without this check we could spin infinitely
            // here looking for a key slot on a full kvs.
            if (resizeRequired()) {
                delete desiredKey;
                return nullptr;
            }

            // reprobe
            idx = clip(idx + 1);
            slot = &mKvs[idx];
        }
        return slot;
    }

    V insertValue(Slot<K, V>* slot, V value, DataState valueState) {
        assert(valueState == COPIED_ALIVE || valueState == ALIVE);
        DataWrapper<V> const* desiredValue =
            new DataWrapper<V>(value, valueState);

        while (true) {
            DataWrapper<V> const* currentValue = slot->value();

            bool const canReplaceWithValueFromOldKvs =
                (currentValue->empty() || currentValue->fromPrevKvs());
            bool const insertingValueFromOldKvs = valueState == COPIED_ALIVE;

            if (!canReplaceWithValueFromOldKvs && insertingValueFromOldKvs) {
                delete desiredValue;
                return currentValue->data();
            }

            // TODO: Is the dereference safe?
            // TODO: Why is this safe! Maybe it isn't maybe it is.
            if (currentValue->eval(desiredValue->data())) {
                // Value already in place so we're done.
                delete desiredValue;
                return currentValue->data();
            }

            if (slot->casValue(currentValue, desiredValue))
                // TODO: SHould we here delete current value?
                return desiredValue->data();
        }
    }

    V insertKvs(std::pair<K, V> const& val, DataState const valueState) {
        Slot<K, V>* slot = insertKey(val.first);
        if (slot == nullptr) {
            // We failed to get a keySlot and a resize is required. Let's start
            // again and check if we can use the new kvs or allocate one
            // ourselves.
            return insert(val);
        }
        return insertValue(slot, val.second, valueState);
    }

    bool eraseKvs(K const key) {
        int slotIdx = hash(key);

        while (true) {
            auto const slotKey = mKvs[slotIdx].key();

            if (slotKey->eval(key)) {
                // great we found it.
                break;
            }

            if (slotKey->empty()) {
                // Couldn't find it, seems the key doesn't exist.
                return false;
            }

            // reprobe
            slotIdx = clip(slotIdx + 1);
        }

        DataWrapper<V>* tombStone = new DataWrapper<V>(V(), TOMB_STONE);
        while (true) {
            auto& slot = mKvs[slotIdx];
            DataWrapper<V> const* slotValue = slot.value();

            // If we find a TOMB_STONE somebody else has already deleted the
            // value, so we can return true, we're done.
            if (slotValue->state() == TOMB_STONE) {
                delete tombStone;
                return true;
            }

            // If we find a COPIED_DEAD the value has been copied into a new
            // table so we need to return false to ensure we check the newer
            // table.
            if (slotValue->state() == COPIED_DEAD) {
                delete tombStone;
                return false;
            }

            auto const valueData = slotValue->data();
            if (slot.casValue(slotValue, tombStone)) {
                // TODO: This shouldn't caus a segfault.
                // delete slotValue;
                return true;
            }
        }

        assert(false);
    }

    V insert(std::pair<K, V> const& val, DataState const valueState) {
        if (resizeRequired()) {
            newKvs();
        }

        // Resized table has been allocated so we should instead insert into
        // that.
        if (mNextKvs != nullptr) {
            // We ask each inserter to also do a little work copying data to the
            // new Kvs.
            copyBatch();
            return nextKvs()->insert(val, valueState);
        }

        return insertKvs(val, valueState);
    }

    bool resizeRequired() const {
        return size() >= mKvs.size() * mMaxLoadRatio;
    }

    size_t clip(size_t const slot) const {
        // mKvs.size() has to be a power of 2.
        // So subtracing 1 gives us a sequence of 1s and then &
        // gives us a size_t between 0 and mKvs.size()
        return slot & (mKvs.size() - 1);
    }

    std::atomic<size_t> mSize{};
    std::vector<Slot<K, V>> mKvs;
    std::atomic<KeyValueStore*> mNextKvs = nullptr;
    std::atomic<size_t> mCopyIdx;
    std::atomic<size_t> mNumReaders = 0;
    // Copied doesn't need to be atomic because it's only every going to change
    // from false to true. and it doesn't matter how many times that happends
    bool mCopied = false;
    float const mMaxLoadRatio;
    std::hash<K> mHash;
};

template <typename K, typename V>
ConcurrentUnorderedMap<K, V>::ConcurrentUnorderedMap(int exp,
                                                     float maxLoadRatio)
    : mHeadKvs(new KeyValueStore<K, V>(std::pow(2, exp), maxLoadRatio)) {}

template <typename K, typename V>
V ConcurrentUnorderedMap<K, V>::insert(std::pair<K, V> const& val) {
    tryUpdateKvsHead();
    return mHeadKvs.load()->insert(val);
}
template <typename K, typename V>
V ConcurrentUnorderedMap<K, V>::at(K const key) const {
    return mHeadKvs.load()->at(key);
}
template <typename K, typename V>
size_t ConcurrentUnorderedMap<K, V>::bucket_count() const {
    return mHeadKvs.load()->bucket_count();
}

template <typename K, typename V>
size_t ConcurrentUnorderedMap<K, V>::size() const {
    return mHeadKvs.load()->size();
}

template <typename K, typename V>
bool ConcurrentUnorderedMap<K, V>::empty() const {
    return mHeadKvs.load()->empty();
}

template <typename K, typename V>
size_t ConcurrentUnorderedMap<K, V>::depth() const {
    size_t depth = 0;
    KeyValueStore<K, V>* kvs = mHeadKvs;
    while (true) {
        if (kvs->nextKvs() == nullptr) {
            break;
        }
        kvs = kvs->nextKvs();
        depth++;
    }
    return depth;
}
template <typename K, typename V>
bool ConcurrentUnorderedMap<K, V>::operator==(
    std::unordered_map<K, V> const& other) const {
    if (size() != other.size()) return false;

    for (auto const& pair : other) {
        try {
            if (mHeadKvs.load()->at(pair.first) != pair.second) {
                return false;
            }

        } catch (std::out_of_range) {
            return false;
        }
    }
    return true;
}

template <typename K, typename V>
void ConcurrentUnorderedMap<K, V>::erase(K const key) {
    mHeadKvs.load()->erase(key);
}

template <typename K, typename V>
void ConcurrentUnorderedMap<K, V>::tryUpdateKvsHead() {
    // Surgically replace the head.
    auto headKvs = mHeadKvs.load();
    auto nextKvs = headKvs->nextKvs();
    if (nextKvs != nullptr && headKvs->copied() &&
        !headKvs->hasActiveReaders()) {
        if (mHeadKvs.compare_exchange_strong(headKvs, nextKvs)) {
            // We won so it's out responsibility to clean up the old Kvs
            // TODO NB: Will need to put this back, but currently it creates
            // segfault sometimes. delete headValue;/* ; */
        }
    }
}

// Explicitly instantiate all the template pairs supported.
// More type pairs should work but these are the tested types.
template class KeyValueStore<int, int>;
template class KeyValueStore<float, float>;
template class KeyValueStore<int, float>;
template class KeyValueStore<float, int>;
template class KeyValueStore<std::vector<bool>, float>;

template class ConcurrentUnorderedMap<float, float>;
template class ConcurrentUnorderedMap<int, int>;
template class ConcurrentUnorderedMap<int, float>;
template class ConcurrentUnorderedMap<float, int>;
template class ConcurrentUnorderedMap<std::vector<bool>, float>;
}  // namespace cmap
