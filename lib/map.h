#ifndef MAP_H
#define MAP_H

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

class Map {
  public:
    Map(int capacity=100) : mCapacity(capacity), mData(std::vector<KeyValuePair>(capacity)) {}

    uint64_t size() const { return mSize.load(); }

    int insert(const int putKey, const int putValue) {
        int slot = hash(putKey, mCapacity);
        auto &pair = mData[slot];
        while (true) {
            auto &k = pair.mKey;
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
        }

        while (true) {
            auto &v = pair.mValue;

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

    int get(const int key) const {
        const auto slot = hash(key, mCapacity);
        return mData[slot].mValue.load();
    }

  private:
    std::atomic<uint64_t> mSize{};
    std::vector<KeyValuePair> mData;
    int mCapacity;
    int mSentryValue = -1;
};
#endif // MAP_H
