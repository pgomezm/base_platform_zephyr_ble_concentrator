# Queue

@defgroup grp_utils Utils
@brief Generic helpers with no hardware dependency

`utils::Queue<T, CAPACITY>` is a fixed-capacity FIFO with its storage inline in the object. No
dynamic allocation, capacity fixed at compile time.

It is used where a module needs its own buffering *inside* one thread. It is deliberately **not**
used for passing events between threads: that is what `eda::Port` and the active object's `k_msgq`
are for, and they already handle the cross-thread case correctly.

Not thread-safe by itself. A queue shared across threads must be guarded by its owner, the same
way `svc::device_table` guards its table with a mutex.
