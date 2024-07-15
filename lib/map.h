#ifndef MAP_H
#define MAP_H

#include <limits>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <vector>

inline const static int *EMPTY = new int(42);
inline const static int *TOMBSTONE = new int(42);

class KeyValuePair {
  public:
    // TODO: Add some comments here on what these are!

    std::atomic<const int *> mKey{};
    std::atomic<const int *> mValue{};

    KeyValuePair() {
        mKey.store(EMPTY);
        mValue.store(EMPTY);
    }
};

inline int hash(const int key, const int capacity) {
    // ZZZ: This isn't even a hash function!
    return key >> (capacity - 1);
}

class ConcurrentUnorderedMap {
  public:
    typedef std::size_t size_type;

    ConcurrentUnorderedMap(int exp = 5)
        : mKvs(std::vector<KeyValuePair>(std::pow(2, exp))) {}

    uint64_t size() const { return mSize.load(); }

    bool empty() const { return mSize.load() == 0; }

    std::size_t bucket_count() const { return mKvs.size(); }

    // According to the spec this should return: pair<iterator,bool> insert (
    // const value_type& val );
    int insert(const std::pair<int, int> &val) {
        const int *putKey = new int(val.first);
        const int *putValue = new int(val.second);
        int slot = hash(*putKey, mKvs.size());
        auto *pair = &mKvs[slot];

        while (true) {
            auto &k = pair->mKey;
            // Check if we've found an open space:
            if (k == EMPTY) {
                // Not 100% sure what the difference is here between _strong and
                // _weak:
                // https://stackoverflow.com/questions/4944771/stdatomic-compare-exchange-weak-vs-compare-exchange-strong

                if (auto success = k.compare_exchange_strong(EMPTY, putKey)) {
                    ++mSize;
                    break;
                }
            }

            // Maybe the key is already inserted?
            // TODO: Reason about if it's safe to dereference the key here?
            // I think it is because key's in a single slot should never change.
            if (*k == *putKey) {
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
            auto &v = pair->mValue;

            // TODO: Is the dereference safe?
            if (*v == *putValue) {
                // Value already in place so we're done.
                delete putValue;
                return *v;
            }

            const int *currentValue = v.load();
            if (auto success =
                    v.compare_exchange_strong(currentValue, putValue)) {
                // We replaced the old value with a new one, so cleanup the old
                // value.
                if (currentValue != EMPTY) {
                    delete currentValue;
                }
                return *putValue;
            }
        }
    }
    int at(const int key) const {
        int slot = hash(key, mKvs.size());
        while (true) {
            const auto &d = mKvs[slot];
            const auto currentKeyValue = d.mKey.load();
            if (*currentKeyValue == key) {
                return *d.mValue.load();
            }
            if (currentKeyValue == EMPTY) {
                throw std::out_of_range("Unables to find key");
            }
            slot = clip(slot + 1);
        }
        // TODO: I think we need to check here if what we load from value is
        // EMPTY, because it could be if another thread inserts a key before
        // this thread looksup the key but the value hasn't been written into
        // place by the other thread.
        return *mKvs[slot].mValue.load();
    }

    // This should be templated to handle different types of maps.
    bool operator==(const std::unordered_map<int, int> &other) const {
        for (const auto &pair : other) {
            try {
                if (this->at(pair.first) != pair.second) {
                    return false;
                }

            } catch (std::out_of_range) {
                return false;
            }
        }
        return true;
    }

  private:
    size_type clip(const size_type slot) const {
        // TODO: Add comment here on how this works?
        return slot & (mKvs.size() - 1);
    }
    std::atomic<uint64_t> mSize{};

    std::vector<KeyValuePair> mKvs;

};

#endif // MAP_H
