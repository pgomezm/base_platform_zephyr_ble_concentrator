/// @addtogroup grp_utils
/// @{
///
/// @file queue.hpp
///
/// Header file that declares a fixed-capacity queue.

#pragma once

#include <cstddef>
#include <cstdint>

namespace utils
{

/// A fixed-capacity FIFO queue with no dynamic allocation.
///
/// Capacity is a template parameter so the storage is part of the object and
/// its size is visible at compile time, which is what makes the memory budget
/// in docs/ARCHITECTURE.md section 4.1 auditable.
///
/// Not thread-safe on its own: the owner is responsible for serialising access,
/// either by confining the queue to one thread or by guarding it.
///
/// @tparam T the element type, must be trivially copyable
/// @tparam CAPACITY the maximum number of elements held at once
template <typename T, size_t CAPACITY>
class Queue
{
public:
    /// Constructor
    Queue() : m_storage{}, m_head{0U}, m_tail{0U}, m_count{0U}
    {}

    /// Push an element to the back of the queue.
    ///
    /// @param item the element to copy in
    /// @return true if it was stored, false if the queue was full
    bool push(const T& item)
    {
        if (is_full())
        {
            return false;
        }

        m_storage[m_tail] = item;
        m_tail = next_index(m_tail);
        ++m_count;

        return true;
    }

    /// Pop the element at the front of the queue.
    ///
    /// @param out_item where the element is copied to
    /// @return true if an element was returned, false if the queue was empty
    bool pop(T& out_item)
    {
        if (is_empty())
        {
            return false;
        }

        out_item = m_storage[m_head];
        m_head = next_index(m_head);
        --m_count;

        return true;
    }

    /// Discard every element.
    void clear()
    {
        m_head = 0U;
        m_tail = 0U;
        m_count = 0U;
    }

    /// @return true if the queue holds no elements
    bool is_empty() const
    {
        return m_count == 0U;
    }

    /// @return true if the queue cannot accept another element
    bool is_full() const
    {
        return m_count == CAPACITY;
    }

    /// @return the number of elements currently held
    size_t size() const
    {
        return m_count;
    }

    /// @return the maximum number of elements
    static constexpr size_t capacity()
    {
        return CAPACITY;
    }

private:
    /// Advance a ring index, wrapping at capacity.
    ///
    /// @param index the index to advance
    /// @return the next index
    static size_t next_index(size_t index)
    {
        return (index + 1U) % CAPACITY;
    }

    /// Element storage
    T m_storage[CAPACITY];

    /// Index of the front element
    size_t m_head;

    /// Index where the next element is written
    size_t m_tail;

    /// Number of elements currently held
    size_t m_count;
};

} // namespace utils

/// @}
