#pragma once
// core/audio - SDL_mixer implementation of core::IAudio.
#include <SDL2/SDL_mixer.h>
#include <map>
#include <string>
#include <vector>
#include "core/audio/audio_api.h"
#include "engine/module.h"

namespace core {

class Audio : public engine::Module, public IAudio {
public:
    const char* name() const override { return "core/audio"; }
    int priority() const override { return -1300; }
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;

    bool active() const override { return active_; }
    float masterVolume() const override { return master_; }
    float busVolume(Bus b) const override { return busPct(b); }
    void addSound(const std::string& name, const std::vector<int16_t>& mono44k) override;
    bool hasSound(const std::string& name) override { return chunkFor(name) != nullptr; }
    void play(const std::string& sound, float volume, Bus bus) override;
    int playLoop(const std::string& sound, float volume, Bus bus) override;
    void setLoopVolume(int loop, float volume) override;
    void stopLoop(int loop) override;

private:
    struct Sound { std::vector<int16_t> mono; Mix_Chunk* chunk = nullptr; std::vector<int16_t> stereo; bool fromFile = false; };
    struct Loop { bool used = false; Bus bus = Bus::Engine; float volume = 0; };
    static constexpr int kLoops = 4;

    Mix_Chunk* chunkFor(const std::string& name);
    float busPct(Bus b) const { return b == Bus::Sfx ? sfx_ : engine_; }
    void applyLoopVolumes();
    void readSettings(engine::Engine&);

    bool active_ = false;
    float master_ = 80, sfx_ = 80, engine_ = 50;
    std::map<std::string, Sound> sounds_;
    Loop loops_[kLoops];
};

} // namespace core
