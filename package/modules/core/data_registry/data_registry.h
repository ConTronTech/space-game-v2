#pragma once
// core/data_registry - implementation of core::IData.
#include <map>
#include <string>
#include <vector>
#include "core/data_registry/data_api.h"
#include "engine/module.h"

namespace core {

class DataRegistry : public engine::Module, public IData {
public:
    const char* name() const override { return "core/data_registry"; }
    int priority() const override { return -1200; }
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;

    bool has(const std::string& category, const std::string& id) const override;
    const engine::Json& get(const std::string& category, const std::string& id) const override;
    std::vector<std::string> ids(const std::string& category) const override;
    std::vector<std::string> categories() const override;
    bool reload() override;

    void setDir(const std::string& d) { dir_ = d; }   // default "data" (or --data=<dir>)

private:
    struct Category {
        std::vector<std::string> order;
        std::map<std::string, engine::Json> defs;
    };
    void loadFile(const std::string& path, const std::string& category);

    std::string dir_ = "data";
    std::map<std::string, Category> cats_;
    engine::Engine* eng_ = nullptr;
};

} // namespace core
