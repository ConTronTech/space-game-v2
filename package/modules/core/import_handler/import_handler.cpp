#include "core/import_handler/import_handler.h"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "engine/engine.h"
#include "engine/math.h"

namespace core {

// ---- built-in loaders ----
static std::shared_ptr<Mesh> loadObj(const std::string& path) {
    std::ifstream f(path);
    if (!f) return nullptr;
    std::vector<engine::Vec3> v, vn;
    auto mesh = std::make_shared<Mesh>();
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v")  { engine::Vec3 p; ss >> p.x >> p.y >> p.z; v.push_back(p); }
        else if (tag == "vn") { engine::Vec3 n; ss >> n.x >> n.y >> n.z; vn.push_back(n); }
        else if (tag == "f") {
            struct Idx { int v = 0, n = 0; };
            std::vector<Idx> face;
            std::string tok;
            while (ss >> tok) {
                Idx i;
                size_t a = tok.find('/');
                i.v = std::atoi(tok.substr(0, a).c_str());
                size_t b = a == std::string::npos ? a : tok.find('/', a + 1);
                if (b != std::string::npos) i.n = std::atoi(tok.substr(b + 1).c_str());
                if (i.v < 0) i.v += (int)v.size() + 1;   // negative = relative index
                if (i.n < 0) i.n += (int)vn.size() + 1;
                face.push_back(i);
            }
            for (size_t k = 1; k + 1 < face.size(); k++) { // fan-triangulate
                Idx tri[3] = {face[0], face[k], face[k + 1]};
                engine::Vec3 fn;
                bool ok = true;
                for (auto& t : tri) if (t.v < 1 || t.v > (int)v.size()) ok = false;
                if (!ok) continue;
                fn = engine::normalize(engine::cross(v[tri[1].v - 1] - v[tri[0].v - 1],
                                                     v[tri[2].v - 1] - v[tri[0].v - 1]));
                for (auto& t : tri) {
                    auto p = v[t.v - 1];
                    auto n = (t.n >= 1 && t.n <= (int)vn.size()) ? vn[t.n - 1] : fn;
                    mesh->positions.insert(mesh->positions.end(), {p.x, p.y, p.z});
                    mesh->normals.insert(mesh->normals.end(), {n.x, n.y, n.z});
                }
            }
        }
    }
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
    if (it == loaders_.end()) { std::fprintf(stderr, "[import] no loader for '%s'\n", ext.c_str()); return {nullptr, typeid(void)}; }

    Loaded asset = it->second(root_ + path);
    if (!asset.data) { std::fprintf(stderr, "[import] failed to load %s%s\n", root_.c_str(), path.c_str()); return {nullptr, typeid(void)}; }
    cache_.insert_or_assign(path, asset);
    return asset;
}

void ImportHandler::reportTypeMismatch(const std::string& path) const {
    std::fprintf(stderr, "[import] %s was loaded as a different type than requested\n", path.c_str());
}

REGISTER_MODULE(ImportHandler);

} // namespace core
