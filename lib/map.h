#ifndef MAP_H
#define MAP_H

#include <iostream>
#include <limits>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <vector>

// TODO: Add some comments here on what these are!
inline const static int *EMPTY = new int(42);
inline const static int *TOMBSTONE = new int(42);

class Node {
  public:
    Node() : mValue(0), mEmpty(true) {}
    Node(int value) : mValue(value), mEmpty(false) {}

    int mValue;
    // TODO: This should probably be a state.
    bool empty() const { return mEmpty; }

  private:
    bool mEmpty = true;
};

class KeyValuePair {
  public:
    std::atomic<const Node *> mKey{};
    std::atomic<const Node *> mValue{};

    KeyValuePair() {

        mKey.store(new Node());
        mValue.store(new Node());
    }
};

inline int hash(const int key, const int capacity) {
    // TODO: Add a real hash function.
    return key >> (capacity - 1);
}

class Impl {
  public:
    typedef std::size_t size_type;

    Impl(int size) : mKvs(std::vector<KeyValuePair>(size)) {}

    uint64_t size() const {
        std::size_t s = mSize;
        if (nextKvs != nullptr) {
            s += nextKvs.load()->size();
        }
        return s;
    }

    bool empty() const {
        auto empty = mSize == 0;
        auto next_empty = true;
        if (nextKvs != nullptr) {
            next_empty = nextKvs.load()->empty();
        }
        return empty && next_empty;
    }

    std::size_t bucket_count() const {
        if (nextKvs != nullptr) {
            return nextKvs.load()->bucket_count();
        }
        return mKvs.size();
    }

    bool resizeRequired() const { return size() > mKvs.size() * mMaxLoad; }

    void allocateNewKvs() {

        // You could check here if anybody else has already started a resize and
        // if so not allocate memory.

        auto *ptr = new Impl(mKvs.size() * 2);
        Impl *x = nullptr;
        // Only thread should win the race and put the newKvs into place.
        auto s = nextKvs.compare_exchange_strong(x, ptr);
        if (!s) {
            // Allocated for nothing, some other thread beat us,
            // so cleanup our mess.
            delete ptr;
        }
    }

    // According to the spec this should return: pair<iterator,bool> insert (
    // const value_type& val );
    int insert(const std::pair<int, int> &val) {
        if (resizeRequired()) {
            allocateNewKvs();
        }
        if (nextKvs != nullptr) {
            return nextKvs.load()->insert(val);
        }
        const Node *putKey = new Node(val.first);
        const Node *putValue = new Node(val.second);
        int slot = hash(putKey->mValue, mKvs.size());
        auto *pair = &mKvs[slot];

        while (true) {
            const Node *k = pair->mKey.load();
            // Check if we've found an open space:
            if (k->empty()) {
                // Not 100% sure what the difference is here between _strong and
                // _weak:
                // https://stackoverflow.com/questions/4944771/stdatomic-compare-exchange-weak-vs-compare-exchange-strong

				// ZZZ: Pull some of this below out in multiple lines.
                if (auto success = pair->mKey.compare_exchange_strong(k, putKey)) {
                    ++mSize;
                    break;
                }
            }

            // Maybe the key is already inserted?
            // TODO: Reason about if it's safe to dereference the key here?
            // I think it is because key's in a single slot should never change.
            if (k->mValue == putKey->mValue) {
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
            const Node* v = pair->mValue.load();

            // TODO: Is the dereference safe?
			// TODO: Why is this safe! Maybe it isn't maybe it is.
            if (v->mValue == putValue->mValue) {
                // Value already in place so we're done.
                delete putValue;
                return v->mValue;
            }

            // const int currentValue = v->mValue;
            if (auto success =
                    pair->mValue.compare_exchange_strong(v, putValue)) {
                // We replaced the old value with a new one, so cleanup the old
                // value.
				// TODO: How should I cleanup v here?
                // if (currentValue != EMPTY) {
                //     delete currentValue;
                // }
                return putValue->mValue;
            }
        }
    }
    int at(const int key) const {
        int slot = hash(key, mKvs.size());
        while (true) {
            const auto &d = mKvs[slot];
            const auto currentKeyValue = d.mKey.load();
            if (currentKeyValue->mValue == key) {
                // TODO: I think we need to check here if what we load from
                // value is EMPTY, because it could be if another thread inserts
                // a key before this thread looksup the key but the value hasn't
                // been written into place by the other thread.
                return d.mValue.load()->mValue;
            }
            if (currentKeyValue->empty() ) {
                if (nextKvs == nullptr)
                    throw std::out_of_range("Unable to find key");
                else
                    return nextKvs.load()->at(key);
            }
            slot = clip(slot + 1);
        }
        assert(false);
    }

  private:
    float mMaxLoad = 0.5;
    size_type clip(const size_type slot) const {
        // TODO: Add comment here on how this works?
        return slot & (mKvs.size() - 1);
    }

    std::atomic<uint64_t> mSize{};
    std::vector<KeyValuePair> mKvs;
    std::atomic<Impl *> nextKvs = nullptr;
};

class ConcurrentUnorderedMap {
  public:
    ConcurrentUnorderedMap(int exp = 5) {
        // ZZZ: This can be in init list
        head = new Impl(std::pow(2, exp));
    }

    std::atomic<Impl *> head;

    int insert(const std::pair<int, int> &val) {
        return head.load()->insert(val);
    }

    int at(const int key) const { return head.load()->at(key); }

    std::size_t bucket_count() const { return head.load()->bucket_count(); }

    std::size_t size() const { return head.load()->size(); }

    std::size_t empty() const { return head.load()->empty(); }

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
