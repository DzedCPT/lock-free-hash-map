#ifndef MAP_H
#define MAP_H

#include "consts.h"
#include "kvs.h"
#include "atomic_kvs_ptr.h"
#include <unordered_map>

namespace cmap {

template <typename K, typename V>
class ConcurrentUnorderedMap {
   public:
    ConcurrentUnorderedMap(int exp = 5,
                           float maxLoadRatio = DEFAULT_MAX_LOAD_RATIO);

    V insert(std::pair<K, V> const& val);
    V at(K key);
    std::size_t bucket_count();
    std::size_t size() const;
    bool empty();
    std::size_t depth();

    bool operator==(std::unordered_map<K, V> const& other) const;
    void erase(K const key);

   private:
    void tryUpdateKvsHead();
    SmartPointer<KeyValueStore<K, V>> mHeadKvs;
};
}  // namespace cmap

#endif  // MAP_H
