#ifndef MAP_H
#define MAP_H

#include <functional>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

const float DEFAULT_MAX_LOAD_RATIO = 0.5;
const size_t COPY_CHUNK_SIZE = 8;


template <typename K, typename V> class KeyValueStore;

template <typename K, typename V> class ConcurrentUnorderedMap {
  public:
    ConcurrentUnorderedMap(int exp = 5, float maxLoadRatio = DEFAULT_MAX_LOAD_RATIO);

    V insert(const std::pair<K, V> &val);
    V at(const K key) const;
    std::size_t bucket_count() const;
    std::size_t size() const;
    std::size_t empty() const;
    std::size_t depth() const;

    bool operator==(const std::unordered_map<K, V> &other) const;
    void erase(const K key);

  private:
    void tryUpdateKvsHead();
    std::atomic<KeyValueStore<K, V> *> mHeadKvs;
};

#endif // MAP_H
