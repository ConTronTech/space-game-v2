#pragma once
// core/import_handler - the single door for loading assets.
// Loaders are registered per file extension, so supporting a new format = one registerLoader() call
// from any module (no edits here).
//     auto& imp = eng.services.require<core::ImportHandler>();
//     auto mesh = imp.load<core::Mesh>("models/ship.obj");     // relative to assets/, cached
//     imp.registerLoader<MyModel>(".glb", [](const std::string& path){ return std::make_shared<MyModel>(...); });
// Asking for the wrong type (load<Mesh>("pic.png")) returns nullptr with a message, never garbage.
#include <GL/gl.h>
#include <functional>
#include <memory>
#include <typeindex>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/import_handler/obj_parser.h"
#include "engine/module.h"

namespace core {

struct Mesh {                       // flat triangle list
    std::vector<float> positions;   // xyz per vertex
    std::vector<float> normals;     // xyz per vertex
    std::vector<float> colors;      // rgba per vertex from the .mtl (Kd, d); empty when the source had no colours
    std::vector<TaggedQuad> tagged; // faces of '@' materials (custom content), NOT part of the arrays above - see obj_parser.h
};
struct Texture { GLuint id = 0; int w = 0, h = 0; };
struct TextAsset { std::string text; };

class ImportHandler : public engine::Module {
public:
    struct Loaded { std::shared_ptr<void> data; std::type_index type = typeid(void); };
    using Loader = std::function<Loaded(const std::string& fullPath)>;

    const char* name() const override { return "core/import_handler"; }
    std::vector<std::string> dependencies() const override { return {"core/window"}; }
    int priority() const override { return -200; }
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;

    template <class T>
    void registerLoader(const std::string& ext, std::function<std::shared_ptr<T>(const std::string&)> fn) {
        loaders_[ext] = [fn = std::move(fn)](const std::string& p) { return Loaded{fn(p), typeid(T)}; };
    }
    void setRoot(const std::string& dir) { root_ = dir; }

    // T must match what the loader for that extension returns. nullptr on failure.
    template <class T>
    std::shared_ptr<T> load(const std::string& path) {
        Loaded l = loadRaw(path);
        if (!l.data) return nullptr;
        if (l.type != std::type_index(typeid(T))) { reportTypeMismatch(path); return nullptr; }
        return std::static_pointer_cast<T>(l.data);
    }
    void clearCache() { cache_.clear(); }

private:
    Loaded loadRaw(const std::string& path);
    void reportTypeMismatch(const std::string& path) const;
    std::unordered_map<std::string, Loader> loaders_;
    std::unordered_map<std::string, Loaded> cache_;
    std::string root_ = "assets/";
};

} // namespace core
