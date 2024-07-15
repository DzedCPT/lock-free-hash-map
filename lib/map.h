#ifndef MAP_H
#define MAP_H

#include <stdexcept>
#include <unordered_map>
#include <vector>

class KeyValuePair {
  public:
    std::atomic<int> mKey{};
    std::atomic<int> mValue{};

    KeyValuePair() {
        // ZZZ: Will need better sentry values here sometime.
        mKey.store(-1);
        mValue.store(-1);
    }
};

inline int hash(const int key, const int capacity) { return key % capacity; }

class ConcurrentUnorderedMap {
  public:
    ConcurrentUnorderedMap(int capacity = 64)
        : mCapacity(capacity), mData(std::vector<KeyValuePair>(capacity)) {}

    uint64_t size() const { return mSize.load(); }

    bool empty() const { return mSize.load() == 0; }

    // According to the spec this should return: pair<iterator,bool> insert (
    // const value_type& val );
    int insert(const std::pair<int, int> &val) {
        int putKey = val.first;
        int putValue = val.second;
        int slot = hash(putKey, mCapacity);
        auto *pair = &mData[slot];
        while (true) {
            auto &k = pair->mKey;
            // Check if we've found an open space:
            if (k == -1) {
                // Not 100% sure what the difference is here between _strong and
                // _weak:
                // https://stackoverflow.com/questions/4944771/stdatomic-compare-exchange-weak-vs-compare-exchange-strong

                if (auto success =
                        k.compare_exchange_strong(mSentryValue, putKey)) {
                    ++mSize;
                    break;
                }
            }

            // Maybe the key is already inserted?
            if (k == putKey) {
                break;
            }

            slot = (slot + 1) % mCapacity;
            pair = &mData[slot];
        }

        while (true) {
            auto &v = pair->mValue;

            if (v == putValue) {
                // Value already in place so we're done.
                return putValue;
            }

            auto currentValue = v.load();
            if (auto success =
                    v.compare_exchange_strong(currentValue, putValue)) {
                return putValue;
            }
        }
    }
    int at(const int key) const {
        int slot = hash(key, mCapacity);
        while (true) {
            const auto &d = mData[slot];
            const auto currentKeyValue = d.mKey.load();
            if (currentKeyValue == key) {
                return d.mValue.load();
            }
            if (currentKeyValue == mSentryValue) {
                throw std::out_of_range("Unables to find key");
            }
            slot = (slot + 1) % mCapacity;
        }
        return mData[slot].mValue.load();
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
    std::atomic<uint64_t> mSize{};
    std::vector<KeyValuePair> mData;
    int mCapacity;
    int mSentryValue = -1;
};

#endif // MAP_H
