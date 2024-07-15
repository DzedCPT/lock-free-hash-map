#ifndef MAP_H
#define MAP_H

#include <unordered_map>

namespace cmap {

float const DEFAULT_MAX_LOAD_RATIO = 0.5;
size_t const COPY_CHUNK_SIZE = 8;

template <typename K, typename V>
class KeyValueStore;

template <typename K, typename V>
class ConcurrentUnorderedMap {
   public:
    ConcurrentUnorderedMap(int exp = 5,
                           float maxLoadRatio = DEFAULT_MAX_LOAD_RATIO);

    V insert(std::pair<K, V> const& val);
    V at(K const key) const;
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
