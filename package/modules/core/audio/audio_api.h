#pragma once
// Sound. Files go in assets/sounds/<name>.wav (or .ogg); code refers to them by name - no code per sound.
//     auto* audio = eng.services.get<core::IAudio>();          // may be null if the module is off: check it
//     audio->play("ui_click");                                  // one-shot
//     int hum = audio->playLoop("engine_loop", 0.0f);           // looping sound, returns a handle (0 = failed)
//     audio->setLoopVolume(hum, throttle);                      // each frame
//     audio->stopLoop(hum);
// Volume (0..1) is multiplied by the bus volume and the master volume, which players set in the menu
// (settings audio.master / audio.sfx / audio.engine, 0..100). With no audio device every call is a harmless no-op.
#include <cstdint>
#include <string>
#include <vector>

namespace core {

enum class Bus { Sfx, Engine };

class IAudio {
public:
    virtual ~IAudio() = default;
    virtual bool active() const = 0;                                                    // false: no audio device
    virtual float masterVolume() const = 0;                                             // 0..100 (players change it via settings audio.master)
    virtual float busVolume(Bus bus) const = 0;                                         // 0..100 (audio.sfx / audio.engine)
    virtual void addSound(const std::string& name, const std::vector<int16_t>& mono44k) = 0;   // synthesized sounds
    virtual bool hasSound(const std::string& name) = 0;                                 // built in, added, or a file
    virtual void play(const std::string& sound, float volume = 1.0f, Bus bus = Bus::Sfx) = 0;
    virtual int playLoop(const std::string& sound, float volume = 1.0f, Bus bus = Bus::Engine) = 0;
    virtual void setLoopVolume(int loop, float volume) = 0;
    virtual void stopLoop(int loop) = 0;
};

} // namespace core
