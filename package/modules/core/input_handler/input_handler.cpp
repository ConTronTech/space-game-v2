#include "core/input_handler/input_handler.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

bool InputHandler::init(engine::Engine& eng) {
    profile_ = eng.flagValue("input-profile", "default");
    eng.services.provide<IInput>(this);
    return true;
}

void InputHandler::shutdown(engine::Engine& eng) { eng.services.withdraw<IInput>(); }

void InputHandler::onFrameBegin(engine::Engine&) {
    // Methods register during init(); load the profile on the first frame so all of them exist.
    if (needLoad_) { needLoad_ = false; loadProfile(profile_); }
    prev_.swap(cur_);
    cur_.clear();
    for (auto& [dev, m] : methods_) m->poll(*this);
    // Keys already held when the game starts (or a profile loads) are not fresh presses.
    if (firstPoll_) { prev_ = cur_; firstPoll_ = false; }
}

float InputHandler::value(const std::string& a) const {
    auto it = cur_.find(a);
    return it == cur_.end() ? 0.0f : clamp1(it->second);
}
float InputHandler::prevValue(const std::string& a) const {
    auto it = prev_.find(a);
    return it == prev_.end() ? 0.0f : clamp1(it->second);
}
bool InputHandler::pressed(const std::string& a) const {
    return std::abs(value(a)) > kThreshold && !(std::abs(prevValue(a)) > kThreshold);
}
bool InputHandler::released(const std::string& a) const {
    return !(std::abs(value(a)) > kThreshold) && std::abs(prevValue(a)) > kThreshold;
}

bool InputHandler::loadProfile(const std::string& name) {
    std::string path = "config/input/" + name + ".json";
    std::ifstream f(path);
    if (!f) { LOG_E("input", "cannot open %s - no bindings loaded", path.c_str()); return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    engine::Json root = engine::Json::parse(ss.str(), &err);
    if (!root.isObject()) { LOG_E("input", "%s: %s", path.c_str(), err.empty() ? "expected a JSON object" : err.c_str()); return false; }

    for (auto& [dev, m] : methods_) m->clearBindings();
    profile_ = name;
    firstPoll_ = true;

    const engine::Json& devices = root["devices"];
    for (auto& dev : devices.keys()) {
        auto it = methods_.find(dev);
        if (it != methods_.end()) it->second->configure(devices[dev]);
    }

    int count = 0, bad = 0;
    std::unordered_set<std::string> warned;
    const engine::Json& actions = root["bindings"];
    for (auto& action : actions.keys()) {
        const engine::Json& list = actions[action];
        for (size_t i = 0; i < list.size(); i++) {
            const engine::Json& b = list.at(i);
            std::string dev = b["device"].str();
            auto it = methods_.find(dev);
            if (it == methods_.end()) {
                if (warned.insert(dev).second)
                    LOG_W("input", "%s: no input method for device '%s' (module not loaded?) - skipping those bindings", path.c_str(), dev.c_str());
                continue;
            }
            std::string berr;
            if (it->second->addBinding(action, b, berr)) count++;
            else { bad++; LOG_W("input", "%s: action '%s' #%zu: %s", path.c_str(), action.c_str(), i + 1, berr.c_str()); }
        }
    }
    LOG_I("input", "profile '%s' (%s): %d bindings%s", name.c_str(), root["name"].str().c_str(), count, bad ? " (some invalid, see above)" : "");
    return true;
}

REGISTER_MODULE(InputHandler);

} // namespace core
