// __CATEGORY__/__NAME__ - describe what this module adds in one line.
#include "engine/engine.h"
// #include "core/input_handler/input_handler.h"   // include the services you use

class __CLASS__ : public engine::Module {
public:
    const char* name() const override { return "__CATEGORY__/__NAME__"; }
    // std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }

    bool init(engine::Engine& eng) override {
        // Look up services, bind input actions, register render passes / UI panels here.
        // Return false to disable this module (the game keeps running unless required() is true).
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        // Undo anything you registered elsewhere (removePass / removePanel / withdraw).
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {}  // physics, fixed 60Hz
    void onUpdate(engine::Engine& eng, float dt) override {}       // game logic, once per frame
};

REGISTER_MODULE(__CLASS__);
