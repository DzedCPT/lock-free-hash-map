#include "kvs.h"

template <typename K, typename V>
KeyValueStore<K, V>::KeyValueStore(size_t size, float maxLoadRatio)
    : mKvs(std::vector<Slot<K, V>>(size)), mMaxLoadRatio(maxLoadRatio) {}

template <typename K, typename V>
size_t KeyValueStore<K, V>::size() const {
    size_t s = mSize;
    if (!mNextKvs.isNull()) {
        s += mNextKvs->size();
    }
    return s;
}

template <typename K, typename V>
bool KeyValueStore<K, V>::empty() const {
    auto empty = mSize == 0;
    auto next_empty = true;
    if (!mNextKvs.isNull()) {
        next_empty = mNextKvs->empty();
    }
    return empty && next_empty;
}

template <typename K, typename V>
size_t KeyValueStore<K, V>::bucket_count() const {
    if (!mNextKvs.isNull()) {
        return mNextKvs->bucket_count();
    }
    return mKvs.size();
}

template <typename K, typename V>
V KeyValueStore<K, V>::insert(std::pair<K, V> const& val) {
    return insert(val, ALIVE);
}

template <typename K, typename V>
void KeyValueStore<K, V>::erase(K const key) {
    if (eraseKvs(key)) {
        return;
    }
    if (!mNextKvs.isNull()) mNextKvs->erase(key);
}

template <typename K, typename V>
V KeyValueStore<K, V>::atKvs(K const key) {
    int idx = hash(key);
    while (true) {
        auto const& slot = mKvs[idx];
        auto const currentKeyValue = slot.key();
        if (currentKeyValue->eval(key)) {
            auto value = slot.value();
            if (value->dead()) {
                if (mNextKvs.isNull()) {
                    mNumReaders--;
                    throw std::out_of_range("Unable to find key");
                } else {
                    return mNextKvs->at(key);
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
            if (mNextKvs.isNull()) {
                throw std::out_of_range("Unable to find key");
            } else {
                auto result = mNextKvs->at(key);
                return result;
            }
        }
        idx = clip(idx + 1);
    }
    assert(false);
}

// template <typename K, typename V>
// KeyValueStore<K, V>* KeyValueStore<K, V>::mNextKvs const {
//     return mNextKvs;
// }

template <typename K, typename V>
bool KeyValueStore<K, V>::copied() const {
    return mCopied;
}

template <typename K, typename V>
bool KeyValueStore<K, V>::hasActiveReaders() const {
    return mNumReaders != 0;
}

template <typename K, typename V>
size_t KeyValueStore<K, V>::hash(K const key) const {
    return clip(mHash(key));
}

template <typename K, typename V>
void KeyValueStore<K, V>::newKvs() {
    // You could check here if anybody else has already started a resize and
    // if so not allocate memory.

    auto ptr = SmartPointer<KeyValueStore<K, V>>(new KeyValueStore(mKvs.size() * 2, mMaxLoadRatio));
    SmartPointer<KeyValueStore<K, V>> nullValue;


    // Only thread should win the race and put the newKvs into place.
    if (!mNextKvs.compare_exchange_strong(nullValue, ptr)) {
        // Allocated for nothing, some other thread beat us,
        // so cleanup our mess.
        // delete ptr;
    }
}

template <typename K, typename V>

size_t KeyValueStore<K, V>::getCopyBatchIdx() {
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
template <typename K, typename V>

void KeyValueStore<K, V>::copySlot(size_t idx) {
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
    DataWrapper<V>* valueCopiedMarker = new DataWrapper<V>(V(), COPIED_DEAD);

    // key wasn't EMPTY so we need to forward the value into the new table.
    while (true) {
        auto value = slot->value();
        auto data = value->data();

        // Some assertions for my sanity.
        assert(!slot->key()->empty());
        assert(!slot->key()->dead());
        assert(value->state() != COPIED_DEAD);
        assert(!mNextKvs.isNull());

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
            mNextKvs->insert({key->data(), data}, COPIED_ALIVE);
            mSize--;
            return;
        }
    }
    assert(false);
}
template <typename K, typename V>

void KeyValueStore<K, V>::copyBatch() {
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

template <typename K, typename V>
Slot<K, V>* KeyValueStore<K, V>::insertKey(K const key) {
    DataWrapper<K> const* desiredKey = new DataWrapper<K>(key, ALIVE);
    int idx = hash(desiredKey->data());
    auto* slot = &mKvs[idx];

    while (true) {
        DataWrapper<K> const* currentKey = slot->key();
        // Check if we've fo und an open space:
        if (currentKey->empty()) {
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

template <typename K, typename V>

V KeyValueStore<K, V>::insertValue(Slot<K, V>* slot, V value,
                                   DataState valueState) {
    assert(valueState == COPIED_ALIVE || valueState == ALIVE);
    DataWrapper<V> const* desiredValue = new DataWrapper<V>(value, valueState);

    while (true) {
        DataWrapper<V> const* currentValue = slot->value();

        bool const canReplaceWithValueFromOldKvs =
            (currentValue->empty() || currentValue->fromPrevKvs());
        bool const insertingValueFromOldKvs = valueState == COPIED_ALIVE;

        if (!canReplaceWithValueFromOldKvs && insertingValueFromOldKvs) {
            delete desiredValue;
            return currentValue->data();
        }

        if (currentValue->eval(desiredValue->data())) {
            // Value already in place so we're done.
            delete desiredValue;
            return currentValue->data();
        }

        if (slot->casValue(currentValue, desiredValue))
            return desiredValue->data();
    }
}

template <typename K, typename V>
V KeyValueStore<K, V>::insertKvs(std::pair<K, V> const& val,
                                 DataState const valueState) {
    Slot<K, V>* slot = insertKey(val.first);
    if (slot == nullptr) {
        // We failed to get a keySlot and a resize is required. Let's start
        // again and check if we can use the new kvs or allocate one
        // ourselves.
        return insert(val);
    }
    return insertValue(slot, val.second, valueState);
}

template <typename K, typename V>

bool KeyValueStore<K, V>::eraseKvs(K const key) {
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
            mSize--;
            return true;
        }
    }

    assert(false);
}

template <typename K, typename V>
V KeyValueStore<K, V>::insert(std::pair<K, V> const& val,
                              DataState const valueState) {
    if (resizeRequired()) {
        newKvs();
    }

    // Resized table has been allocated so we should instead insert into
    // that.
    if (!mNextKvs.isNull()) {
        // We ask each inserter to also do a little work copying data to the
        // new Kvs.
        copyBatch();
        return mNextKvs->insert(val, valueState);
    }

    return insertKvs(val, valueState);
}

template <typename K, typename V>
V KeyValueStore<K, V>::at(K const key) {
    if (mCopied) {
        // Not possible to be copied and not have a nextKvs, because
        // otherwise where did we copy everything into.
        assert(!mNextKvs.isNull());
        return mNextKvs->at(key);
    }

    mNumReaders++;
    auto const result = atKvs(key);
    mNumReaders--;
    return result;
}

template <typename K, typename V>
bool KeyValueStore<K, V>::resizeRequired() const {
    return size() >= mKvs.size() * mMaxLoadRatio;
}

template <typename K, typename V>
size_t KeyValueStore<K, V>::clip(size_t const slot) const {
    // mKvs.size() has to be a power of 2.
    // So subtracing 1 gives us a sequence of 1s and then &
    // gives us a size_t between 0 and mKvs.size()
    return slot & (mKvs.size() - 1);
}

// Explicitly instantiate all the template pairs supported.
// More type pairs should work but these are the tested types.
template class KeyValueStore<int, int>;
template class KeyValueStore<float, float>;
template class KeyValueStore<int, float>;
template class KeyValueStore<float, int>;
template class KeyValueStore<std::vector<bool>, float>;
