#pragma once
// ui/toast - generic notification popups (bottom-right corner, newest at the bottom, fade out). See docs/TOAST.md.
// An OPTIONAL service: a module that wants toasts looks it up and does nothing when it is absent.
//
//     if (auto* t = eng.services.get<core::IToast>())
//         t->show("Recipe learned: FUEL CELL");                               // Info, default lifetime (toast.seconds)
//     t->show("Hull breach", core::IToast::Level::Urgent, 6.0f);             // red, flashing, 6 s
#include <string>
#include "ui/toast/toast_rules.h"

namespace core {

class IToast {
public:
    using Level = toast::Level;       // Info (calm cyan) | Warning (amber) | Urgent (red, flashing)
    virtual ~IToast() = default;
    // Queue a toast. seconds <= 0 uses toast.seconds. Safe to call every frame only if you mean it: each call is a new toast.
    virtual void show(const std::string& text, Level level = Level::Info, float seconds = 0) = 0;
    virtual void clear() = 0;         // remove every visible and waiting toast
};

} // namespace core
