#ifndef MAP_H
#define MAP_H

#include "kvs.h"
#include <unordered_map>
#include "kvs.h"
#include "consts.h"

namespace cmap {

template <typename K, typename V>
class ConcurrentUnorderedMap {
   public:
    ConcurrentUnorderedMap(int exp = 5,
                           float maxLoadRatio = DEFAULT_MAX_LOAD_RATIO);

    V insert(std::pair<K, V> const& val);
    V at(K key) const;
    std::size_t bucket_count() const;
    std::size_t size() const;
    bool empty() const;
    std::size_t depth() const;

    bool operator==(std::unordered_map<K, V> const& other) const;
    void erase(K const key);

   private:
    void tryUpdateKvsHead();
    std::atomic<KeyValueStore<K, V>*> mHeadKvs;
};
}  // namespace cmap

#endif  // MAP_H
