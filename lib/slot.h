
#include "data_wrapper.h"
#include <atomic>

#ifndef SLOT_H
#define SLOT_H

template <typename K, typename V>
class Slot {
   public:
    Slot() {
        mKey.store(new DataWrapper<K>(K(), EMPTY));
        mValue.store(new DataWrapper<V>(V(), EMPTY));
    }

    ~Slot() {
        delete mKey.load();
        delete mValue.load();
    }

    bool casValue(DataWrapper<V> const* expected,
                  DataWrapper<V> const* desired) {
        auto const success = mValue.compare_exchange_strong(expected, desired);
        if (success) delete expected;
        return success;
    }

    bool casKey(DataWrapper<K> const* expected, DataWrapper<K> const* desired) {
        bool const success = mKey.compare_exchange_strong(expected, desired);
        if (success) delete expected;
        return success;
    }

    DataWrapper<K> const* key() const { return mKey.load(); }

    DataWrapper<V> const* value() const { return mValue.load(); }

   private:
    std::atomic<DataWrapper<K> const*> mKey{};
    std::atomic<DataWrapper<V> const*> mValue{};
};

#endif // SLOT_H
