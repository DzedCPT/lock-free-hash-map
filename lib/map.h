#ifndef MAP_H
#define MAP_H

#include <limits>
#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

typedef std::size_t size_t;
const float MAX_LOAD_FACTOR = 0.5;
const size_t COPY_CHUNK_SIZE = 8;

enum DataState {
    EMPTY,
    ALIVE,
    COPIED,
};

class DataWrapper {
  public:
    DataWrapper(int value) : mData(value), mState(ALIVE) {}
    DataWrapper(DataState state) : mData(0), mState(state) {}

    bool empty() const { return mState == EMPTY; }
    bool copied() const { return mState == COPIED; }
    bool alive() const { return mState == ALIVE; }
    int data() const { return mData; }

  private:
    const int mData = 0;
    DataState mState = EMPTY;
};

class Slot {
  public:
    // TODO: Slot needs a desctructor.
    Slot() {
        mKey.store(new DataWrapper(EMPTY));
        mValue.store(new DataWrapper(EMPTY));
    }

    bool casValue(const DataWrapper *expected, const DataWrapper *desired) {
        return mValue.compare_exchange_strong(expected, desired);
    }

    bool casKey(const DataWrapper *expected, const DataWrapper *desired) {
        return mKey.compare_exchange_strong(expected, desired);
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
    KeyValueStore(int size) : mKvs(std::vector<Slot>(size)) {}

    size_t size() const {
        size_t s = mSize;
        if (mNextKvs != nullptr) {
            s += mNextKvs.load()->size();
        }
        return s;
    }

    bool empty() const {
        auto empty = mSize == 0;
        auto next_empty = true;
        if (mNextKvs != nullptr) {
            next_empty = mNextKvs.load()->empty();
        }
        return empty && next_empty;
    }

    size_t bucket_count() const {
        if (mNextKvs != nullptr) {
            return mNextKvs.load()->bucket_count();
        }
        return mKvs.size();
    }

    // According to the spec this should return: pair<iterator,bool> insert (
    // const value_type& val );
    int insert(const std::pair<int, int> &val) {
        if (resizeRequired()) {
            allocateNewKvs();
        }
        if (mNextKvs != nullptr) {
            doCopyWork();
            return mNextKvs.load()->insert(val);
        }
        const DataWrapper *putKey = new DataWrapper(val.first);
        const DataWrapper *putValue = new DataWrapper(val.second);
        int slot = hash(putKey->data(), mKvs.size());
        auto *pair = &mKvs[slot];

        while (true) {
            const DataWrapper *k = pair->key();
            // Check if we've found an open space:
            if (k->empty()) {
                // Not 100% sure what the difference is here between _strong and
                // _weak:
                // https://stackoverflow.com/questions/4944771/stdatomic-compare-exchange-weak-vs-compare-exchange-strong

                // ZZZ: Pull some of this below out in multiple lines.
                if (auto success = pair->casKey(k, putKey)) {
                    ++mSize;
                    break;
                }
            }

            // Maybe the key is already inserted?
            // TODO: Reason about if it's safe to dereference the key here?
            // I think it is because key's in a single slot should never change.
            if (k->data() == putKey->data()) {
                // The current key has the same value as the one were trying to
                // insert. So we can just use the current key but need to not
                // leak the memory of the newly allocated key.
                delete putKey;
                break;
            }

            slot = clip(slot + 1);
            pair = &mKvs[slot];
        }

        while (true) {
            const DataWrapper *v = pair->value();

            // TODO: Is the dereference safe?
            // TODO: Why is this safe! Maybe it isn't maybe it is.
            if (v->data() == putValue->data()) {
                // Value already in place so we're done.
                delete putValue;
                return v->data();
            }

            // const int currentValue = v->mValue;
            if (auto success = pair->casValue(v, putValue)) {
                // We replaced the old value with a new one, so cleanup the old
                // value.
                // TODO: How should I cleanup v here?
                // if (currentValue != EMPTY) {
                //     delete currentValue;
                // }
                return putValue->data();
            }
        }
    }
    int at(const int key) {
        if (mCopied) {
            // Not possible to be copied and not have a nextKvs, because
            // otherwise where did we copy everything into.
            assert(mNextKvs != nullptr);
            return mNextKvs.load()->at(key);
        }

        mNumReaders++;
        int slot = hash(key, mKvs.size());
        while (true) {
            const auto &d = mKvs[slot];
            const auto currentKeyValue = d.key();
            if (currentKeyValue->data() == key && currentKeyValue->alive()) {
                auto value = d.value();
                if (value->copied()) {
                    if (mNextKvs == nullptr) {
                        mNumReaders--;
                        throw std::out_of_range("Unable to find key");
                    } else {
                        auto result = mNextKvs.load()->at(key);
                        mNumReaders--;
                        return result;
                    }
                }
                // TODO: I think we need to check here if what we load from
                // value is EMPTY, because it could be if another thread inserts
                // a key before this thread looksup the key but the value hasn't
                // been written into place by the other thread.
                return value->data();
            }
            if (currentKeyValue->empty() || currentKeyValue->copied()) {
                if (mNextKvs == nullptr) {
                    mNumReaders--;
                    throw std::out_of_range("Unable to find key");
                } else {

                    auto result = mNextKvs.load()->at(key);
                    mNumReaders--;
                    return result;
                }
            }
            slot = clip(slot + 1);
        }
        assert(false);
    }

    KeyValueStore *getNextKvs() const { return mNextKvs.load(); }

    bool copied() const { return mCopied; }

    bool hasActiveReaders() const { return mNumReaders != 0; }

  private:
    void allocateNewKvs() {

        // You could check here if anybody else has already started a resize and
        // if so not allocate memory.

        auto *ptr = new KeyValueStore(mKvs.size() * 2);
        KeyValueStore *x = nullptr;
        // Only thread should win the race and put the newKvs into place.
        auto s = mNextKvs.compare_exchange_strong(x, ptr);
        if (!s) {
            // Allocated for nothing, some other thread beat us,
            // so cleanup our mess.
            delete ptr;
        }
    }

    std::optional<size_t> getCopyWork() {
        auto startIdx = mCopyIdx.load();
        if (startIdx >= mKvs.size()) {
            return std::nullopt;
        }

        size_t endIdx = startIdx + COPY_CHUNK_SIZE;
        if (!mCopyIdx.compare_exchange_strong(startIdx, endIdx)) {
            // Another thread claimed this work before us.
            return std::nullopt;
        }
        return startIdx;
    }

    void doCopy(size_t slot) {

        DataWrapper *copiedMarker = new DataWrapper(COPIED);
        // TODO: This loop should never try twice, so you could assert that.
        while (true) {
            auto pair = &mKvs[slot];
            auto key = pair->key();

            assert(!key->copied());
            // first case the slot has no key.
            if (key->empty()) {
                auto succes = pair->casKey(key, copiedMarker);
                if (succes) {
                    return;
                }
                continue;
            }

            // Second case the slot has a key.
            // We need to copy the value to the new table.
            break;
        }

        while (true) {
            auto pair = &mKvs[slot];
            auto key = pair->key();
            assert(!key->empty());
            assert(!key->copied());

            auto value = pair->value();
            assert(!value->copied());
            assert(!value->empty());
            assert(mNextKvs != nullptr);

            auto success = pair->casValue(value, copiedMarker);
            if (success) {
                // TODO: This will currently overwrite the value in the new
                // table if a new value has been written after the copy started.
                mNextKvs.load()->insert({key->data(), value->data()});
                mSize--;
                return;
            }
        }
    }

    void doCopyWork() {

        const auto workStartIdxOpt = getCopyWork();
        if (!workStartIdxOpt.has_value()) {
            // No work to do.
            return;
        }

        auto workStartIdx = workStartIdxOpt.value();
        for (auto i = workStartIdx; i < workStartIdx + COPY_CHUNK_SIZE; i++) {
            doCopy(i);
        }

        if (workStartIdx + COPY_CHUNK_SIZE == mKvs.size()) {
            mCopied = true;
        }
    }

    bool resizeRequired() const {
        return size() > mKvs.size() * MAX_LOAD_FACTOR;
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
};

class ConcurrentUnorderedMap {
  public:
    ConcurrentUnorderedMap(int exp = 5)
        : head(new KeyValueStore(std::pow(2, exp))) {}

    std::atomic<KeyValueStore *> head;

    int insert(const std::pair<int, int> &val) {
        // Surgically replace the head.
        auto headValue = head.load();
        auto nextKvs = headValue->getNextKvs();
        if (nextKvs != nullptr && headValue->copied() &&
            !headValue->hasActiveReaders()) {
            // if (nextKvs != nullptr && headValue->copied() ) {
            // if (nextKvs != nullptr && headValue->copied()) {
            // The current head is dead, since it's been copied into the kvs.
            // So we need to
            auto success = head.compare_exchange_strong(headValue, nextKvs);
            if (success) {
                // We won so it's out responsibility to clean up the old Kvs
                // TODO NB: Will need to put this back, but currently it creates
                // segfault sometimes. delete headValue;/* ; */
            }
        }

        return head.load()->insert(val);
    }

    int at(const int key) const { return head.load()->at(key); }

    size_t bucket_count() const { return head.load()->bucket_count(); }

    size_t size() const { return head.load()->size(); }

    size_t empty() const { return head.load()->empty(); }

    size_t depth() const {
        size_t depth = 0;
        KeyValueStore *x = head;
        while (true) {
            if (x->getNextKvs() == nullptr) {
                break;
            }
            x = x->getNextKvs();
            depth++;
        }
        return depth;
    }

    // This should be templated to handle different types of maps.
    bool operator==(const std::unordered_map<int, int> &other) const {
        for (const auto &pair : other) {
            try {
                if (head.load()->at(pair.first) != pair.second) {
                    return false;
                }

            } catch (std::out_of_range) {
                return false;
            }
        }
        return true;
    }
};

#endif // MAP_H
