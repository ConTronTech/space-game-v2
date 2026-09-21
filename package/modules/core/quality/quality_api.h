#pragma once
// core::IQuality - what the quality module decided, for the pause menu / benchmark / anyone who wants to show it.
//     auto* q = eng.services.get<core::IQuality>();        // null if core/quality is off
//     q->activeName()  -> "high";  q->selectedName() -> "auto" (what the player asked for)
#include <string>

namespace core {

class IQuality {
public:
    virtual ~IQuality() = default;
    virtual std::string selectedName() const = 0;      // "auto" | "low" | ... : the choice in effect this run (flag > settings > game.json)
    virtual std::string activeName() const = 0;        // "low" | "medium" | "high" | "ultra": the preset actually applied this run
    virtual std::string detectedName() const = 0;      // what Auto picks on this machine
    virtual std::string reason() const = 0;            // why (hardware summary)
};

} // namespace core
