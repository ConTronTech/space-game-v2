#pragma once
// core/import_handler - the single door for loading assets.
// Loaders are registered per file extension, so supporting a new format = one registerLoader() call
// from any module (no edits here).
//     auto& imp = eng.services.require<core::ImportHandler>();
//     auto mesh = imp.load<core::Mesh>("models/ship.obj");     // relative to assets/, cached
//     imp.registerLoader(".glb", [](const std::string& path){ return std::make_shared<MyModel>(...); });
#include <GL/gl.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "engine/module.h"

namespace core {

struct Mesh {                       // flat triangle list
    std::vector<float> positions;   // xyz per vertex
    std::vector<float> normals;     // xyz per vertex
};
struct Texture { GLuint id = 0; int w = 0, h = 0; };
struct TextAsset { std::string text; };

class ImportHandler : public engine::Module {
public:
    using Loader = std::function<std::shared_ptr<void>(const std::string& fullPath)>;

    const char* name() const override { return "core/import_handler"; }
    std::vector<std::string> dependencies() const override { return {"core/window"}; }
    int priority() const override { return -200; }
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;

    void registerLoader(const std::string& ext, Loader fn) { loaders_[ext] = std::move(fn); }
    void setRoot(const std::string& dir) { root_ = dir; }

    // T must match what the loader for that extension returns. nullptr on failure.
    template <class T>
    std::shared_ptr<T> load(const std::string& path) {
        return std::static_pointer_cast<T>(loadRaw(path));
    }
    void clearCache() { cache_.clear(); }

private:
    std::shared_ptr<void> loadRaw(const std::string& path);
    std::unordered_map<std::string, Loader> loaders_;
    std::unordered_map<std::string, std::shared_ptr<void>> cache_;
    std::string root_ = "assets/";
};

} // namespace core
