#pragma once
// core/settings - implementation of core::ISettings (see settings_api.h).
#include <map>
#include <string>
#include "core/settings/settings_api.h"
#include "engine/module.h"

namespace core {

class Settings : public engine::Module, public ISettings {
public:
    const char* name() const override { return "core/settings"; }
    int priority() const override { return -1500; }   // before everything, so owners can read their values in init()
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;
    void onUpdate(engine::Engine&, float) override;

    const engine::Json& raw(const std::string& key) const override;
    void setRaw(const std::string& key, engine::Json value) override;
    void save() override;

    void setPath(const std::string& p) { path_ = p; }   // default config/settings.json (or --settings=<file>)
    bool load();

private:
    std::string path_ = "config/settings.json";
    std::map<std::string, engine::Json> values_;
    engine::Engine* eng_ = nullptr;
    bool dirty_ = false;
    double lastChange_ = 0;
};

} // namespace core
