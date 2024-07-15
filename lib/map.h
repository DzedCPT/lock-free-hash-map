#ifndef MAP_H
#define MAP_H

#include <iostream>
#include <limits>
#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

enum NodeState {
    EMPTY,
    ALIVE,
    COPIED,
};
class Node {
  public:
    Node() : mValue(0), mState(EMPTY) {}
    Node(int value) : mValue(value), mState(ALIVE) {}
    Node(NodeState copied) : mState(copied) {}

    int mValue = 0;
    bool empty() const { return mState == EMPTY; }
    bool copied() const { return mState == COPIED; }
    bool alive() const { return mState == ALIVE; }

  private:
    NodeState mState = EMPTY;
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

    std::optional<std::size_t> getCopyWork() {
        auto startIdx = mCopyIdx.load();
        if (startIdx >= mKvs.size()) {
            return std::nullopt;
        }

        std::size_t endIdx = startIdx + COPY_CHUNK_SIZE;
        if (!mCopyIdx.compare_exchange_strong(startIdx, endIdx)) {
            // Another thread claimed this work before us.
            return std::nullopt;
        }
        return startIdx;
    }

    void doCopy(std::size_t slot) {

        Node *copiedMarker = new Node(COPIED);
        // TODO: This loop should never try twice, so you could assert that.
        while (true) {
            auto pair = &mKvs[slot];
            auto key = pair->mKey.load();

            assert(!key->copied());
            // first case the slot has no key.
            if (key->empty()) {
                auto succes =
                    pair->mKey.compare_exchange_strong(key, copiedMarker);
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
            auto key = pair->mKey.load();
            assert(!key->empty());
            assert(!key->copied());

            auto value = pair->mValue.load();
            assert(!value->copied());
            assert(!value->empty());
            assert(nextKvs != nullptr);

            auto success =
                pair->mValue.compare_exchange_strong(value, copiedMarker);
            if (success) {
                // TODO: This will currently overwrite the value in the new
                // table if a new value has been written after the copy started.
                nextKvs.load()->insert({key->mValue, value->mValue});
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

    // According to the spec this should return: pair<iterator,bool> insert (
    // const value_type& val );
    int insert(const std::pair<int, int> &val) {
        if (resizeRequired()) {
            allocateNewKvs();
        }
        if (nextKvs != nullptr) {
            doCopyWork();
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
                if (auto success =
                        pair->mKey.compare_exchange_strong(k, putKey)) {
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
            const Node *v = pair->mValue.load();

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
    int at(const int key) {
        if (mCopied) {
            // Not possible to be copied and not have a nextKvs, because
            // otherwise where did we copy everything into.
            assert(nextKvs != nullptr);
            return nextKvs.load()->at(key);
        }

        mNumReaders++;
        int slot = hash(key, mKvs.size());
        while (true) {
            const auto &d = mKvs[slot];
            const auto currentKeyValue = d.mKey.load();
            if (currentKeyValue->mValue == key && currentKeyValue->alive()) {
                auto value = d.mValue.load();
                if (value->copied()) {
                    if (nextKvs == nullptr) {
                        mNumReaders--;
                        throw std::out_of_range("Unable to find key");
                    } else {
                        auto result = nextKvs.load()->at(key);
                        mNumReaders--;
						return result;

                    }
                }
                // TODO: I think we need to check here if what we load from
                // value is EMPTY, because it could be if another thread inserts
                // a key before this thread looksup the key but the value hasn't
                // been written into place by the other thread.
                return value->mValue;
            }
            if (currentKeyValue->empty() || currentKeyValue->copied()) {
                if (nextKvs == nullptr) {
                    mNumReaders--;
                    throw std::out_of_range("Unable to find key");
                } else {

                    auto result = nextKvs.load()->at(key);
                    mNumReaders--;
                    return result;
                }
            }
            slot = clip(slot + 1);
        }
        assert(false);
    }

    Impl *getNextKvs() const { return nextKvs.load(); }

    bool copied() const { return mCopied; }

	bool hasActiveReaders()const {
		return mNumReaders == 0;
	}

  private:
    inline static const std::size_t COPY_CHUNK_SIZE = 8;

    float mMaxLoad = 0.5;
    size_type clip(const size_type slot) const {
        // TODO: Add comment here on how this works?
        return slot & (mKvs.size() - 1);
    }

    std::atomic<uint64_t> mSize{};
    std::vector<KeyValuePair> mKvs;
    std::atomic<Impl *> nextKvs = nullptr;
    std::atomic<std::size_t> mCopyIdx;
    std::atomic<std::size_t> mNumReaders = 0;
	// ZZZ: Why does this need to be volatile.
    volatile bool mCopied = false;
};

class ConcurrentUnorderedMap {
  public:
    ConcurrentUnorderedMap(int exp = 5) : head(new Impl(std::pow(2, exp))) {}

    std::atomic<Impl *> head;

    int insert(const std::pair<int, int> &val) {
		// Surgically replace the head.
        auto headValue = head.load();
        auto nextKvs = headValue->getNextKvs();
        // if (nextKvs != nullptr && headValue->copied() && !headValue->hasActiveReaders()) {
        if (nextKvs != nullptr && headValue->copied() ) {
        // if (nextKvs != nullptr && headValue->copied()) {
            // The current head is dead, since it's been copied into the kvs.
            // So we need to
			auto success = head.compare_exchange_strong(headValue, nextKvs);
			if (success) {
				// We won so it's out responsibility to clean up the old Kvs
				// TODO: Will need to put this back.
				delete headValue;/* ; */

			}
        }

        return head.load()->insert(val);
    }

    int at(const int key) const { return head.load()->at(key); }

    std::size_t bucket_count() const { return head.load()->bucket_count(); }

    std::size_t size() const { return head.load()->size(); }

    std::size_t empty() const { return head.load()->empty(); }

    std::size_t depth() const {
        std::size_t depth = 0;
        Impl *x = head;
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
