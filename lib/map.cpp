#include "map.h"
#include "data_wrapper.h"
#include "slot.h"
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "kvs.h"

namespace cmap {

typedef std::size_t size_t;

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
V ConcurrentUnorderedMap<K, V>::at(const K key) const {
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
