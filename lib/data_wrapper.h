
#ifndef DATA_WRAPPER_H
#define DATA_WRAPPER_H

enum DataState {
    EMPTY,         // The data has not been set
    ALIVE,         // The data is to be used.
    TOMB_STONE,    // The data has been removed.
    COPIED_DEAD,   // The data has been copied from the current location
    COPIED_ALIVE,  // The copy has been copied into the current location.
};

template <typename T>
class DataWrapper {
public:
    DataWrapper(T value, DataState state) : mData(value), mState(state) {}

    bool empty() const {
        return !(mState == ALIVE || mState == COPIED_DEAD ||
                 mState == COPIED_ALIVE);
    }
    bool fromPrevKvs() const { return mState == COPIED_ALIVE; }
    bool dead() const { return mState == COPIED_DEAD || mState == TOMB_STONE; }
    bool eval(T val) const {
        if (mState == ALIVE || mState == COPIED_ALIVE) return val == mData;
        return false;
    }

    // getters
    T data() const { return mData; }
    DataState state() const { return mState; }

private:
    T const mData;
    DataState mState = EMPTY;
};

#endif // DATA_WRAPPER_H
