#include "core/import_handler/import_handler.h"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

// ---- built-in loaders ----
static std::shared_ptr<Mesh> loadObj(const std::string& path) {
    ObjParseResult r;
    if (!parseObjFile(path, r)) return nullptr;
    for (auto& w : r.warnings) LOG_W("import", "%s: %s", path.c_str(), w.c_str());
    auto mesh = std::make_shared<Mesh>();
    mesh->positions = std::move(r.positions);
    mesh->normals = std::move(r.normals);
    mesh->colors = std::move(r.colors);
    mesh->tagged = std::move(r.tagged);
    return mesh;
}

static std::shared_ptr<Texture> loadImage(const std::string& path) {
    SDL_Surface* raw = IMG_Load(path.c_str());
    if (!raw) return nullptr;
    SDL_Surface* s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(raw);
    if (!s) return nullptr;
    auto tex = std::shared_ptr<Texture>(new Texture, [](Texture* t) { // frees GL memory when last user lets go
        if (t->id) glDeleteTextures(1, &t->id);
        delete t;
    });
    tex->w = s->w; tex->h = s->h;
    glGenTextures(1, &tex->id);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, s->pitch / 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->w, s->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    SDL_FreeSurface(s);
    return tex;
}

static std::shared_ptr<TextAsset> loadText(const std::string& path) {
    std::ifstream f(path);
    if (!f) return nullptr;
    auto t = std::make_shared<TextAsset>();
    std::stringstream ss;
    ss << f.rdbuf();
    t->text = ss.str();
    return t;
}

bool ImportHandler::init(engine::Engine& eng) {
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    registerLoader<Mesh>(".obj", loadObj);
    for (auto e : {".png", ".jpg", ".jpeg", ".bmp"}) registerLoader<Texture>(e, loadImage);
    for (auto e : {".txt", ".json", ".cfg"}) registerLoader<TextAsset>(e, loadText);
    eng.services.provide<ImportHandler>(this);
    return true;
}

void ImportHandler::shutdown(engine::Engine& eng) {
    eng.services.withdraw<ImportHandler>();
    cache_.clear(); // frees GL textures while the context still exists
    IMG_Quit();
}

ImportHandler::Loaded ImportHandler::loadRaw(const std::string& path) {
    auto hit = cache_.find(path);
    if (hit != cache_.end()) return hit->second;

    size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto it = loaders_.find(ext);
    if (it == loaders_.end()) { LOG_W("import", "no loader for '%s'", ext.c_str()); return {nullptr, typeid(void)}; }

    Loaded asset = it->second(root_ + path);
    if (!asset.data) { LOG_E("import", "failed to load %s%s", root_.c_str(), path.c_str()); return {nullptr, typeid(void)}; }
    cache_.insert_or_assign(path, asset);
    return asset;
}

void ImportHandler::reportTypeMismatch(const std::string& path) const {
    LOG_W("import", "%s was loaded as a different type than requested", path.c_str());
}

REGISTER_MODULE(ImportHandler);

} // namespace core
