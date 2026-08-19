/// @addtogroup grp_eda
/// @{
///
/// @file idle_hook.cpp
///
/// Idle hook implementation file.

#include "eda/idle_hook/idle_hook.hpp"

namespace eda
{

// Initialize static member
IdleHook::IdleCallback IdleHook::m_callback = nullptr;

void IdleHook::register_callback(IdleCallback callback)
{
    m_callback = callback;
}

void IdleHook::invoke()
{
    if (m_callback != nullptr)
    {
        m_callback();
    }
}

} // namespace eda

/// @}
