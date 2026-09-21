// world/skybox - six textured quads around the camera, faces downscaled to a texture budget for low-end GPUs (shared memory).
// Faces are loaded one at a time, capped at skybox.max_size, and the downscaled copy is cached in cache/skybox/. See docs/WORLD.md.
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/json.h"
#include "engine/log.h"
#include "world/skybox/skybox_rules.h"

namespace fs = std::filesystem;

class Skybox : public engine::Module {
public:
    const char* name() const override { return "world/skybox"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine", "core/import_handler"}; }   // import_handler runs IMG_Init

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("skybox.enabled", true, "draw the skybox")) return true;
        std::string dir = c.get(std::string("skybox.dir"), std::string("assets/skybox/bkg"), "folder with <color>/<set>/ face images");
        std::string want = c.get(std::string("skybox.set"), std::string("dark/set1"), "which skybox: color/set (falls back to the first found)");
        int maxSize = c.get("skybox.max_size", 1024, "largest face size in pixels (longest side); bigger images are downscaled to save graphics memory");
        std::string cacheDir = c.get(std::string("skybox.cache_dir"), std::string("cache"), "where downscaled faces are cached ('' = no cache)");
        auto t0 = std::chrono::steady_clock::now();

        // 1. find the sets (folders with all six faces), sorted so the "first" one is stable
        std::vector<Set> sets;
        std::error_code ec;
        for (auto& col : fs::directory_iterator(dir, ec)) {
            if (!col.is_directory(ec)) continue;
            for (auto& st : fs::directory_iterator(col.path(), ec)) {
                if (!st.is_directory(ec)) continue;
                Set s; s.color = col.path().filename().string(); s.name = st.path().filename().string();
                int found = 0;
                for (auto& f : fs::directory_iterator(st.path(), ec)) {
                    std::string ext = f.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp") continue;
                    int face = world::matchFace(f.path().filename().string());
                    if (face >= 0 && s.files[face].empty()) { s.files[face] = f.path().string(); found++; }
                }
                if (found == 6) { s.dir = st.path().string(); sets.push_back(s); }
            }
        }
        std::sort(sets.begin(), sets.end(), [](const Set& a, const Set& b) { return a.color + "/" + a.name < b.color + "/" + b.name; });
        std::vector<std::string> names;
        for (auto& s : sets) names.push_back(s.color + "/" + s.name);
        int idx = world::chooseSet(names, want);
        if (idx < 0) { LOG_W("skybox", "no skybox sets found in %s: no skybox", dir.c_str()); return true; }
        if (names[idx] != want) LOG_W("skybox", "set '%s' not found, using '%s'", want.c_str(), names[idx].c_str());
        Set& set = sets[idx];
        readUV(set);

        // 2. load faces one by one, capped to the texture budget
        GLint maxTex = 1024;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        int cap = std::max(1, std::min(maxSize, (int)maxTex));
        long bytes = 0; int cached = 0; world::Size shown;
        for (int i = 0; i < 6; i++) {
            bool fromCache = false;
            world::Size sz;
            tex_[i] = loadFace(set, i, cap, cacheDir, sz, fromCache);
            if (!tex_[i]) {
                LOG_W("skybox", "could not load face '%s' of %s: no skybox", set.files[i].c_str(), names[idx].c_str());
                freeTextures();
                return true;
            }
            bytes += (long)sz.w * sz.h * 3; cached += fromCache; shown = sz;
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        LOG_I("skybox", "%s: faces %dx%d (cap %d), %.1f MB of textures, %.0f ms, cache %s", names[idx].c_str(), shown.w, shown.h, cap,
              bytes / 1048576.0, ms, cached == 6 ? "USED" : cached == 0 ? "not used" : "partly used");
        for (int i = 0; i < 6; i++) uv_[i] = world::effectiveFaceUV(i, set.uv[i]);   // sets follow the old game convention (see docs/WORLD.md)
        render_ = &eng.services.require<core::RenderEngine>();
        render_->addPass("skybox", -100, [this](core::RenderEngine& r) { draw(r); });
        active_ = true;
        return true;
    }

    void shutdown(engine::Engine&) override {
        if (active_) render_->removePass("skybox");
        freeTextures();
    }

private:
    struct Set { std::string color, name, dir, files[6]; world::FaceUV uv[6]; };

    // skybox.json: { "top": {"flip_v": true}, "bottom": {"rotate": 90}, ... } (camelCase flipU/flipV also accepted)
    static void readUV(Set& s) {
        std::ifstream f(s.dir + "/skybox.json");
        if (!f) return;
        std::stringstream ss; ss << f.rdbuf();
        std::string err;
        engine::Json j = engine::Json::parse(ss.str(), &err);
        if (!j.isObject()) { LOG_W("skybox", "%s/skybox.json: %s - ignored", s.dir.c_str(), err.c_str()); return; }
        for (int i = 0; i < 6; i++) {
            const engine::Json& o = j[world::faceJsonKey(i)];
            s.uv[i].flipU = o["flip_u"].boolean(o["flipU"].boolean(false));
            s.uv[i].flipV = o["flip_v"].boolean(o["flipV"].boolean(false));
            s.uv[i].rotate = world::normalizeRotate((int)o["rotate"].num(0));
        }
    }

    static int64_t stamp(const fs::path& p, std::error_code& ec) {
        auto t = fs::last_write_time(p, ec);
        return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
    }

    // Returns a GL texture (0 on failure). The big source image is decoded, downscaled and freed before the upload.
    unsigned loadFace(const Set& set, int face, int cap, const std::string& cacheDir, world::Size& outSize, bool& fromCache) {
        const std::string& src = set.files[face];
        std::string cpath = cacheDir.empty() ? "" : world::cachePath(cacheDir, set.color, set.name, cap, world::faceName(face));
        std::vector<uint8_t> pix;
        int w = 0, h = 0;
        std::error_code ec;
        if (!cpath.empty()) {   // warm start: the small cached copy, if it is fresh
            bool exists = fs::exists(cpath, ec);
            int64_t ct = exists ? stamp(cpath, ec) : 0, st = stamp(src, ec);
            if (world::cacheFresh(exists && !ec, ct, st) && loadPacked(cpath, cap, pix, w, h) && w <= cap && h <= cap) fromCache = true;
            else { pix.clear(); }
        }
        if (!fromCache) {
            bool shrunk = false;
            if (!loadPacked(src, cap, pix, w, h, &shrunk)) return 0;
            if (shrunk && !cpath.empty()) saveCache(cpath, pix, w, h);   // only downscaled faces are worth caching
        }
        outSize = {w, h};
        unsigned t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pix.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        return t;
    }

    // Decode `path`, downscale to fit `cap` (area average), return tightly packed RGB. The decoded surface is freed before returning.
    static bool loadPacked(const std::string& path, int cap, std::vector<uint8_t>& out, int& w, int& h, bool* shrunk = nullptr) {
        SDL_Surface* s = IMG_Load(path.c_str());
        if (!s) return false;
        if (s->format->format != SDL_PIXELFORMAT_RGB24 && s->format->format != SDL_PIXELFORMAT_RGBA32) {
            SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
            SDL_FreeSurface(s);
            if (!c) return false;
            s = c;
        }
        int bpp = s->format->BytesPerPixel;
        world::Size d = world::cappedSize(s->w, s->h, cap);
        world::boxDownscale((const uint8_t*)s->pixels, s->w, s->h, s->pitch, bpp, d.w, d.h, out);   // also packs when no scaling is needed
        w = d.w; h = d.h;
        if (shrunk) *shrunk = d.w != s->w || d.h != s->h;
        SDL_FreeSurface(s);
        return true;
    }

    static void saveCache(const std::string& path, std::vector<uint8_t>& pix, int w, int h) {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(pix.data(), w, h, 24, w * 3, SDL_PIXELFORMAT_RGB24);
        if (!s) return;
        if (IMG_SavePNG(s, path.c_str()) != 0) LOG_W("skybox", "cannot write cache %s: %s", path.c_str(), IMG_GetError());
        SDL_FreeSurface(s);
    }

    void freeTextures() {
        for (auto& t : tex_) if (t) { glDeleteTextures(1, &t); t = 0; }
    }

    void quad(int f, const float (&v)[4][3]) {
        static const float c[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        glBindTexture(GL_TEXTURE_2D, tex_[f]);
        glBegin(GL_QUADS);
        for (int k = 0; k < 4; k++) {
            float u, vv;
            uv_[f].transform(c[k][0], c[k][1], u, vv);
            glTexCoord2f(u, vv);
            glVertex3fv(v[k]);
        }
        glEnd();
    }

    void draw(core::RenderEngine& r) {
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        m[12] = m[13] = m[14] = 0;
        glLoadMatrixf(m);
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_LIGHTING);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        const float s = kHalf;
        quad(0, {{-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s}});   // front  (-Z)
        quad(1, {{s, -s, s}, {-s, -s, s}, {-s, s, s}, {s, s, s}});       // back   (+Z)
        quad(2, {{-s, -s, s}, {-s, -s, -s}, {-s, s, -s}, {-s, s, s}});   // left   (-X)
        quad(3, {{s, -s, -s}, {s, -s, s}, {s, s, s}, {s, s, -s}});       // right  (+X)
        quad(4, {{-s, s, -s}, {s, s, -s}, {s, s, s}, {-s, s, s}});       // top    (+Y)
        quad(5, {{-s, -s, s}, {s, -s, s}, {s, -s, -s}, {-s, -s, -s}});   // bottom (-Y)
        glBindTexture(GL_TEXTURE_2D, 0);
        glPopAttrib();
    }

    static constexpr float kHalf = 1000.0f;   // corners at ~1732 m: inside the far plane (20000), and depth is off anyway
    core::RenderEngine* render_ = nullptr;
    bool active_ = false;
    unsigned tex_[6] = {};
    world::FaceUV uv_[6];
};

REGISTER_MODULE(Skybox);
