# Audio

`core/audio` (SDL_mixer) provides `core::IAudio`. Sounds are referred to by **name**; there is no code per sound.

```cpp
auto* audio = eng.services.get<core::IAudio>();       // null if the module is disabled
audio->play("ui_click", 0.6f);                         // one-shot (volume 0..1, default bus Sfx)
int hum = audio->playLoop("engine_loop", 0.0f, core::Bus::Engine);   // up to 4 loops; 0 = failed
audio->setLoopVolume(hum, throttle);                   // call each frame
audio->stopLoop(hum);                                  // in shutdown
```

- **Where sounds come from:** built in (`ui_click`, `ui_confirm`, `engine_loop`, synthesized in code), or files
  `assets/sounds/<name>.wav` / `.ogg`. `audio->addSound(name, samples)` registers a synthesized one (mono, 44.1 kHz, int16).
- **Volume** = your volume x bus x master. Players set them in Pause > Settings (`audio.master`, `audio.sfx`, `audio.engine`, 0-100;
  defaults 80 / 80 / 50). Change them from code with `ISettings::set`; the audio module applies them immediately.
- **No sound card?** The module warns once and every call becomes a harmless no-op. The game never fails because of audio.
- The ship's engine hum (flight) rises with throttle and is silent while the menu is open; the menu plays click sounds.
- Testing: `SDL_AUDIODRIVER=dummy ./space_game_v2` runs the real audio path without speakers (smoke.sh does this).
- Synthesis and the volume math are pure functions in `audio_synth.h` with unit tests.
