#include <unordered_map>

class Map {
  public:
    std::unordered_map<int, int> mData;

    Map() { mData = std::unordered_map<int, int>(); }

    void insert(int k, int v) { mData.insert({k, v}); }
};
