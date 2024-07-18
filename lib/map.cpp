#include "map.h"
#include "data_wrapper.h"
#include "kvs.h"
#include "slot.h"
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace cmap {

typedef std::size_t size_t;

template <typename K, typename V>
ConcurrentUnorderedMap<K, V>::ConcurrentUnorderedMap(int exp,
                                                     float maxLoadRatio)
    : mHeadKvs(new KeyValueStore<K, V>(std::pow(2, exp), maxLoadRatio)) {}

template <typename K, typename V>
V ConcurrentUnorderedMap<K, V>::insert(std::pair<K, V> const& val) {
    tryUpdateKvsHead();
    return mHeadKvs.load().ref()->insert(val);
}
template <typename K, typename V>
V ConcurrentUnorderedMap<K, V>::at(K const key) {
    return mHeadKvs.load().ref()->at(key);
}
template <typename K, typename V>
size_t ConcurrentUnorderedMap<K, V>::bucket_count() {
    return mHeadKvs.load().ref()->bucket_count();
}

template <typename K, typename V>
size_t ConcurrentUnorderedMap<K, V>::size() const {
    return mHeadKvs.constLoad()->size();
}

template <typename K, typename V>
bool ConcurrentUnorderedMap<K, V>::empty() {
    return mHeadKvs.load().ref()->empty();
}

template <typename K, typename V>
size_t ConcurrentUnorderedMap<K, V>::depth() {
    size_t depth = 0;
    auto kvs = mHeadKvs.load();
    while (true) {
        if (kvs.ref()->nextKvs() == nullptr) {
            break;
        }
		// ZZZ: NEed to fix:
        // kvs = kvs.ref()->nextKvs();
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
            if (mHeadKvs.constLoad()->at(pair.first) != pair.second) {
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
    mHeadKvs.load().ref()->erase(key);
}

template <typename K, typename V>
void ConcurrentUnorderedMap<K, V>::tryUpdateKvsHead() {
    // Surgically replace the head.
    auto headKvs = mHeadKvs.load();
    auto nextKvs = headKvs.ref()->nextKvs();
    if (nextKvs != nullptr && headKvs.ref()->copied() &&
        !headKvs.ref()->hasActiveReaders()) {
        if (mHeadKvs.exchange(headKvs.ref(), nextKvs)) {
        // We won so it's out responsibility to clean up the old Kvs
        // TODO NB: Will need to put this back, but currently it creates
        // segfault sometimes. delete headValue;/* ; */
        // delete mHeadKvs.load();
        }
    }
}

template class ConcurrentUnorderedMap<float, float>;
template class ConcurrentUnorderedMap<int, int>;
template class ConcurrentUnorderedMap<int, float>;
template class ConcurrentUnorderedMap<float, int>;
template class ConcurrentUnorderedMap<std::vector<bool>, float>;
}  // namespace cmap
