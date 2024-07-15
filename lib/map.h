#ifndef MAP_H
#define MAP_H

#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

typedef std::size_t size_t;
const float DEFAULT_MAX_LOAD_RATIO = 0.5;
const size_t COPY_CHUNK_SIZE = 8;

enum DataState {
    EMPTY,
    ALIVE,
    COPIED_DEAD,
    COPIED_ALIVE,
};

class DataWrapper {
  public:
    DataWrapper(int value) : mData(value), mState(ALIVE) {}
    DataWrapper(DataState state) : mData(0), mState(state) {}
    DataWrapper(int value, DataState state) : mData(value), mState(state) {}

    bool empty() const {
        return !(mState == ALIVE || mState == COPIED_DEAD ||
                 mState == COPIED_ALIVE);
    }
    bool fromPrevKvs() const { return mState == COPIED_ALIVE; }
    bool dead() const { return mState == COPIED_DEAD; }
    bool alive() const { return mState == ALIVE; }
    int data() const { return mData; }

  private:
    const int mData = 0;
    DataState mState = EMPTY;
};

class Slot {
  public:
    Slot() {
        mKey.store(new DataWrapper(EMPTY));
        mValue.store(new DataWrapper(EMPTY));
    }

    ~Slot() {
        delete mKey.load();
        delete mValue.load();
    }

    bool casValue(const DataWrapper *expected, const DataWrapper *desired) {
        const auto success = mValue.compare_exchange_strong(expected, desired);
        if (success)
            delete expected;
        return success;
    }

    bool casKey(const DataWrapper *expected, const DataWrapper *desired) {
        const bool success = mKey.compare_exchange_strong(expected, desired);
        if (success)
            delete expected;
        return success;
    }

    const DataWrapper *key() const { return mKey.load(); }

    const DataWrapper *value() const { return mValue.load(); }

  private:
    std::atomic<const DataWrapper *> mKey{};
    std::atomic<const DataWrapper *> mValue{};
};

// TODO: Add a real hash function.
inline int hash(const int key, const int capacity) {
    return key >> (capacity - 1);
}

class KeyValueStore {
  public:
    KeyValueStore(int size, float maxLoadRatio)
        : mKvs(std::vector<Slot>(size)), mMaxLoadRatio(maxLoadRatio) {}

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
    int insert(const std::pair<int, int> &val) { return insert(val, ALIVE); }

    int atKvs(const int key) {
        // mNumReaders++;
        int slot = hash(key, mKvs.size());
        while (true) {
            const auto &d = mKvs[slot];
            const auto currentKeyValue = d.key();
            if (currentKeyValue->data() == key && currentKeyValue->alive()) {
                auto value = d.value();
                if (value->dead()) {
                    if (mNextKvs == nullptr) {
                        // TODO: This currently won't correctly decrease the
                        // number of readers.
                        throw std::out_of_range("Unable to find key");
                    } else {
                        auto result = nextKvs()->at(key);
                        return result;
                    }
                }
                // TODO: I think we need to check here if what we load from
                // value is EMPTY, because it could be if another thread inserts
                // a key before this thread looksup the key but the value hasn't
                // been written into place by the other thread.
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

    int at(const int key) {
        if (mCopied) {
            // Not possible to be copied and not have a nextKvs, because
            // otherwise where did we copy everything into.
            assert(mNextKvs != nullptr);
            return nextKvs()->at(key);
        }

        mNumReaders++;
        const auto result = atKvs(key);
        mNumReaders--;
        return result;
    }

    KeyValueStore *nextKvs() const { return mNextKvs.load(); }

    bool copied() const { return mCopied; }

    bool hasActiveReaders() const { return mNumReaders != 0; }

  private:
    void newKvs() {

        // You could check here if anybody else has already started a resize and
        // if so not allocate memory.

        auto *ptr = new KeyValueStore(mKvs.size() * 2, mMaxLoadRatio);
        KeyValueStore *null_lvalue = nullptr;
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
        DataWrapper *copiedMarker = new DataWrapper(COPIED_DEAD);

        Slot *slot = &mKvs[idx];
        auto key = slot->key();
        assert(!key->dead());

        // Let's see if we can put a COPIED state into an EMPTY key:
        if (key->empty()) {
            if (slot->casKey(key, copiedMarker))
                return;
            // Key was EMPTY when we last checked, but not by the time the
            // cas was attempted so we need to copy the value into the new
            // kvs.
        }

        // key wasn't EMPTY so we need to forward the value into the new table.
        while (true) {
            auto value = slot->value();
            auto data = value->data();

            // Some assertions for my sanity.
            assert(!slot->key()->empty());
            assert(!slot->key()->dead());
            assert(!value->dead());
            assert(mNextKvs != nullptr);

            if (value->empty()) {
                // We got here so the key wasn't empty, but the value is empty.
                // This means we've got inbetweeen an insert that had inserted
                // the key but not yet the value.
                // So we need to wait until the value is visible before we can
                // do the cas below.
                continue;
            }

            if (slot->casValue(value, copiedMarker)) {
                nextKvs()->insert({key->data(), data}, COPIED_ALIVE);
                mSize--;
                return;
            }
        }
        assert(false);
    }

    void copyBatch() {
        const auto startIdx = getCopyBatchIdx();
        if (startIdx == mKvs.size()) {
            // Either the copy is done or another thread got the work.
            return;
        }

        const size_t endIdx = startIdx + COPY_CHUNK_SIZE;

        for (auto i = startIdx; i < endIdx; i++)
            copySlot(i);

        if (endIdx == mKvs.size())
            mCopied = true;

        assert(endIdx <= mKvs.size());
    }

    Slot *insertKey(int key) {
        const DataWrapper *desiredKey = new DataWrapper(key);
        int idx = hash(desiredKey->data(), mKvs.size());
        auto *slot = &mKvs[idx];

        while (true) {
            const DataWrapper *currentKey = slot->key();
            // Check if we've found an open space:
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

            if (currentKey->data() == desiredKey->data()) {
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

    int insertValue(Slot *slot, int value, DataState valueState) {
        assert(valueState == COPIED_ALIVE || valueState == ALIVE);
        const DataWrapper *desiredValue = new DataWrapper(value, valueState);

        while (true) {
            const DataWrapper *currentValue = slot->value();

            const bool canReplaceWithValueFromOldKvs =
                (currentValue->empty() || currentValue->fromPrevKvs());
            const bool insertingValueFromOldKvs = valueState == COPIED_ALIVE;

            if (!canReplaceWithValueFromOldKvs && insertingValueFromOldKvs) {
                delete desiredValue;
                return currentValue->data();
            }

            // TODO: Is the dereference safe?
            // TODO: Why is this safe! Maybe it isn't maybe it is.
            if (currentValue->data() == desiredValue->data()) {
                // Value already in place so we're done.
                delete desiredValue;
                return currentValue->data();
            }

            if (slot->casValue(currentValue, desiredValue))
                // TODO: SHould we here delete current value?
                return desiredValue->data();
        }
    }

    int insertKvs(const std::pair<int, int> &val, const DataState valueState) {
        Slot *slot = insertKey(val.first);
        if (slot == nullptr) {
            // We failed to get a keySlot and a resize is required. Let's start
            // again and check if we can use the new kvs or allocate one
            // ourselves.
            return insert(val);
        }
        return insertValue(slot, val.second, valueState);
    }

    int insert(const std::pair<int, int> &val, const DataState valueState) {
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
    size_t clip(const size_t slot) const {
        // TODO: Add comment here on how this works?
        return slot & (mKvs.size() - 1);
    }

    std::atomic<uint64_t> mSize{};
    std::vector<Slot> mKvs;
    std::atomic<KeyValueStore *> mNextKvs = nullptr;
    std::atomic<size_t> mCopyIdx;
    std::atomic<size_t> mNumReaders = 0;
    // ZZZ: Why does this need to be volatile.
    volatile bool mCopied = false;
    const float mMaxLoadRatio;
};

class ConcurrentUnorderedMap {
  public:
    ConcurrentUnorderedMap(int exp = 5,
                           float maxLoadRatio = DEFAULT_MAX_LOAD_RATIO)
        : mHeadKvs(new KeyValueStore(std::pow(2, exp), maxLoadRatio)) {}

    int insert(const std::pair<int, int> &val) {
        tryUpdateKvsHead();
        return mHeadKvs.load()->insert(val);
    }

    int at(const int key) const { return mHeadKvs.load()->at(key); }

    size_t bucket_count() const { return mHeadKvs.load()->bucket_count(); }

    size_t size() const { return mHeadKvs.load()->size(); }

    size_t empty() const { return mHeadKvs.load()->empty(); }

    size_t depth() const {
        size_t depth = 0;
        KeyValueStore *kvs = mHeadKvs;
        while (true) {
            if (kvs->nextKvs() == nullptr) {
                break;
            }
            kvs = kvs->nextKvs();
            depth++;
        }
        return depth;
    }

    // This should be templated to handle different types of maps.
    bool operator==(const std::unordered_map<int, int> &other) const {
        for (const auto &pair : other) {
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

  private:
    void tryUpdateKvsHead() {
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
    std::atomic<KeyValueStore *> mHeadKvs;
};

#endif // MAP_H
