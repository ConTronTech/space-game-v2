#include "core/audio/audio.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include "core/audio/audio_synth.h"
#include "core/settings/settings_api.h"
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

void Audio::readSettings(engine::Engine& eng) {
    if (auto* s = eng.services.get<ISettings>()) {
        master_ = s->get("audio.master", 80.0f);
        sfx_ = s->get("audio.sfx", 80.0f);
        engine_ = s->get("audio.engine", 50.0f);
    }
}

bool Audio::init(engine::Engine& eng) {
    readSettings(eng);
    eng.events.subscribe<SettingChanged>([this, &eng](const SettingChanged& e) {
        if (e.key.rfind("audio.", 0) != 0) return;
        readSettings(eng);
        applyLoopVolumes();
    });

    // built-in sounds, so the game has audio with zero asset files (a file with the same name in assets/sounds/ is NOT used: rename it)
    addSound("ui_click", synth::blip(880.0f, 0.06f));
    addSound("ui_confirm", synth::blip(1320.0f, 0.10f));
    addSound("engine_loop", synth::hum(2.0f));

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 || Mix_OpenAudio(synth::kRate, AUDIO_S16SYS, 2, 1024) != 0) {
        LOG_W("audio", "no audio device (%s) - sound disabled, the game continues silently", SDL_GetError());
        active_ = false;
    } else {
        Mix_AllocateChannels(16);
        Mix_ReserveChannels(kLoops);   // channels 0..3 belong to loops; one-shots use the rest
        active_ = true;
        LOG_I("audio", "ready (master %.0f, effects %.0f, engine %.0f)", master_, sfx_, engine_);
    }
    eng.services.provide<IAudio>(this);   // provided even with no device, so callers never need a null check for "no sound card"
    return true;
}

void Audio::shutdown(engine::Engine& eng) {
    eng.services.withdraw<IAudio>();
    if (active_) {
        Mix_HaltChannel(-1);
        for (auto& [n, s] : sounds_) if (s.chunk) Mix_FreeChunk(s.chunk);
        Mix_CloseAudio();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    sounds_.clear();
    active_ = false;
}

void Audio::addSound(const std::string& name, const std::vector<int16_t>& mono) {
    Sound& s = sounds_[name];
    if (s.chunk) { if (active_) Mix_HaltChannel(-1); Mix_FreeChunk(s.chunk); s.chunk = nullptr; }
    s.mono = mono;
    s.stereo.clear();
    s.fromFile = false;
}

Mix_Chunk* Audio::chunkFor(const std::string& name) {
    if (!active_) return nullptr;
    auto it = sounds_.find(name);
    if (it == sounds_.end()) {
        // not synthesized: look for a file
        for (const char* ext : {".wav", ".ogg"}) {
            std::string path = "assets/sounds/" + name + ext;
            if (Mix_Chunk* c = Mix_LoadWAV(path.c_str())) {
                Sound& s = sounds_[name];
                s.chunk = c; s.fromFile = true;
                return c;
            }
        }
        LOG_W("audio", "unknown sound '%s' (not built in, no assets/sounds/%s.wav|.ogg)", name.c_str(), name.c_str());
        sounds_[name];   // remember the miss so we warn once
        return nullptr;
    }
    Sound& s = it->second;
    if (s.chunk) return s.chunk;
    if (s.mono.empty()) return nullptr;
    s.stereo.resize(s.mono.size() * 2);                       // mono -> interleaved stereo for the mixer
    for (size_t i = 0; i < s.mono.size(); i++) s.stereo[2 * i] = s.stereo[2 * i + 1] = s.mono[i];
    s.chunk = Mix_QuickLoad_RAW((Uint8*)s.stereo.data(), (Uint32)(s.stereo.size() * sizeof(int16_t)));
    return s.chunk;
}

void Audio::play(const std::string& sound, float volume, Bus bus) {
    Mix_Chunk* c = chunkFor(sound);
    if (!c) return;
    int ch = Mix_PlayChannel(-1, c, 0);
    if (ch >= 0) Mix_Volume(ch, (int)(synth::mixGain(master_, busPct(bus), volume) * MIX_MAX_VOLUME));
}

int Audio::playLoop(const std::string& sound, float volume, Bus bus) {
    Mix_Chunk* c = chunkFor(sound);
    if (!c) return 0;
    for (int i = 0; i < kLoops; i++) {
        if (loops_[i].used) continue;
        if (Mix_PlayChannel(i, c, -1) < 0) return 0;
        loops_[i] = {true, bus, volume};
        applyLoopVolumes();
        return i + 1;
    }
    LOG_W("audio", "all %d loop slots are in use", kLoops);
    return 0;
}

void Audio::setLoopVolume(int loop, float volume) {
    if (loop < 1 || loop > kLoops || !loops_[loop - 1].used) return;
    loops_[loop - 1].volume = volume;
    Mix_Volume(loop - 1, (int)(synth::mixGain(master_, busPct(loops_[loop - 1].bus), volume) * MIX_MAX_VOLUME));
}

void Audio::stopLoop(int loop) {
    if (loop < 1 || loop > kLoops || !loops_[loop - 1].used) return;
    if (active_) Mix_HaltChannel(loop - 1);
    loops_[loop - 1].used = false;
}

void Audio::applyLoopVolumes() {
    if (!active_) return;
    for (int i = 0; i < kLoops; i++)
        if (loops_[i].used) Mix_Volume(i, (int)(synth::mixGain(master_, busPct(loops_[i].bus), loops_[i].volume) * MIX_MAX_VOLUME));
}

REGISTER_MODULE(Audio);

} // namespace core
