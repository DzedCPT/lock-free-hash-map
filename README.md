# Thread Safe Lock Free Hash Map

This repository contains a C++ implementation of a hash map.

The design of this hash map is taken from the talk [Advanced Topics in Programming Languages: A Lock-Free Hash Table By  Dr. Cliff Click](https://www.youtube.com/watch?v=HJ-719EGIts) 
- Slides from the talk can be found [here](https://web.stanford.edu/class/ee380/Abstracts/070221_LockFreeHash.pdf). 

What makes the hash map interesting is:
1. It's thread safe.
2. It's lock free.
3. No locking even during resize. 

It achieves the above thanks to some clever use of the atomic CAS (compare and swap).

## Notes From the Talk

- Each slot in the map is an atomic key and value.
- To update the table with a key-value pair you need to CAS in your key and then your value. 
    - By CAS I mean run in a while loop until you can read the atomic data and CAS it for the new data you want to replace it with.
    - THis ensures you've seen the most recent state and made the required update to the table.
- By design once a key is inserted into a slot of the hash map it can never be changed.
    - This ensures that onces you've CAS'd in your key you can be sure that no other thread will change the key while you're trying to CAS in the value. ie once you've claimed a slot for a given key there's no data race that can change the key of that slot.
    - You might have multiple threads competing for the value, but that's allowed.
- Given that keys are fixed each value slot is also given a state which can be toggled to indicate that the value is deleted.
- When increasing the size of the table
    - You create a new table which inserts operations go directly to once the table has been initialized. 
    - Get operations look at the old table, and if the key is not found will look at the new table.
    - During a resize each insert operation will copy (ie insert) some values from the old table into the new table.
- There's a number of states associated with each key and value slot, which are:
    - null: empty
    - K, V: Key or Value data is in the slot.
    - X: the slot has been copied to the new table so gets and inserts need to move to the copy table. This is a way of avoiding new data coming into the old table after a given slot has been copied to a new table. 
    - T: A label to say this value has been deleted: ie a (Tombstone).
    - The resulting state machine for a key value pair (taken from the slides above) is:
    <img width="669" alt="Screenshot 2024-07-07 at 21 22 47" src="https://github.com/DzedCPT/lock-free-hash-map/assets/90834269/2aa8283d-8eed-4cca-a5c9-fe437d03fab6">

