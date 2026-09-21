#pragma once
// Pure skybox rules: no SDL, no GL, no engine types. Unit-tested in package/tests/test_world.cpp.
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace world {

// Faces in draw order: 0 front (-Z), 1 back (+Z), 2 left (-X), 3 right (+X), 4 top (+Y), 5 bottom (-Y).
inline const char* faceName(int i) {
    static const char* n[6] = {"front", "back", "left", "right", "top", "bot"};
    return (i >= 0 && i < 6) ? n[i] : "";
}
// key used in skybox.json ("bottom", not "bot")
inline const char* faceJsonKey(int i) { return i == 5 ? "bottom" : faceName(i); }

// Which face a file name is for (old skybox.h rules); -1 when it is not a face image.
inline int matchFace(const std::string& file) {
    std::string low = file;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    auto dot = low.rfind('.');
    if (dot != std::string::npos) low = low.substr(0, dot);
    auto has = [&](const char* s) { return low.find(s) != std::string::npos; };
    if (has("front") || has("_ft")) return 0;
    if (has("back") || has("_bk")) return 1;
    if (has("left") || has("_lf")) return 2;
    if (has("right") || has("_rt")) return 3;
    if (has("top") || has("_up")) return 4;
    if (has("bot") || has("_dn") || has("down")) return 5;
    return -1;
}

struct Size { int w = 0, h = 0; };

// Longest side capped at maxSize keeping the aspect ratio; never enlarges; maxSize < 1 counts as 1; sides stay >= 1.
inline Size cappedSize(int w, int h, int maxSize) {
    if (maxSize < 1) maxSize = 1;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    int longest = std::max(w, h);
    if (longest <= maxSize) return {w, h};
    double k = (double)maxSize / longest;
    return {std::max(1, (int)(w * k + 0.5)), std::max(1, (int)(h * k + 0.5))};
}

// Per-face UV orientation (old FaceUV): flip U, flip V, then rotate around the centre.
struct FaceUV {
    bool flipU = false, flipV = false;
    int rotate = 0;   // 0 / 90 / 180 / 270
    void transform(float inU, float inV, float& outU, float& outV) const {
        float u = inU, v = inV;
        if (flipU) u = 1.0f - u;
        if (flipV) v = 1.0f - v;
        if (rotate == 90) { float t = u; u = v; v = 1.0f - t; }
        else if (rotate == 180) { u = 1.0f - u; v = 1.0f - v; }
        else if (rotate == 270) { float t = u; u = 1.0f - v; v = t; }
        outU = u; outV = v;
    }
};

// The old game's per-set skybox.json flips are relative to an implicit vertical flip of the top (4) and bottom (5) faces
// (measured: without it every shipped set has visible seams there). effective flipV = implicit XOR json flip_v; everything else untouched.
inline FaceUV effectiveFaceUV(int face, FaceUV json) {
    if (face == 4 || face == 5) json.flipV = !json.flipV;
    return json;
}

// Anything that is not 0/90/180/270 (after wrapping) counts as 0.
inline int normalizeRotate(int deg) {
    deg %= 360;
    if (deg < 0) deg += 360;
    return (deg == 90 || deg == 180 || deg == 270) ? deg : 0;
}

// Choose from "color/set" names: the wanted one if present, else the first, else -1 when the list is empty.
inline int chooseSet(const std::vector<std::string>& names, const std::string& wanted) {
    if (names.empty()) return -1;
    for (size_t i = 0; i < names.size(); i++) if (names[i] == wanted) return (int)i;
    return 0;
}

// cache/skybox/<color>_<set>_<maxsize>/<face>.png
inline std::string cachePath(const std::string& cacheRoot, const std::string& color, const std::string& set, int maxSize, const std::string& face) {
    return cacheRoot + "/skybox/" + color + "_" + set + "_" + std::to_string(maxSize < 1 ? 1 : maxSize) + "/" + face + ".png";
}

// A cache file is usable when it exists and is not older than its source (times in any common unit, e.g. seconds).
inline bool cacheFresh(bool cacheExists, int64_t cacheTime, int64_t sourceTime) {
    return cacheExists && cacheTime >= sourceTime;
}

// Area-average (box) downscale of interleaved 8-bit pixels. src has `bpp` bytes per pixel (3 or 4; only the first 3 channels are
// used) and `srcPitch` bytes per row; dst is tightly packed RGB (dw*dh*3 bytes, resized here).
inline void boxDownscale(const uint8_t* src, int sw, int sh, int srcPitch, int bpp, int dw, int dh, std::vector<uint8_t>& dst) {
    dst.assign((size_t)dw * dh * 3, 0);
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((int64_t)y * sh / dh), y1 = std::max(y0 + 1, (int)(((int64_t)(y + 1) * sh + dh - 1) / dh));
        y1 = std::min(y1, sh);
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((int64_t)x * sw / dw), x1 = std::max(x0 + 1, (int)(((int64_t)(x + 1) * sw + dw - 1) / dw));
            x1 = std::min(x1, sw);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint8_t* p = src + (size_t)yy * srcPitch + (size_t)x0 * bpp;
                for (int xx = x0; xx < x1; xx++, p += bpp) { r += p[0]; g += p[1]; b += p[2]; n++; }
            }
            uint8_t* o = &dst[((size_t)y * dw + x) * 3];
            if (n) { o[0] = (uint8_t)((r + n / 2) / n); o[1] = (uint8_t)((g + n / 2) / n); o[2] = (uint8_t)((b + n / 2) / n); }
        }
    }
}

// ---- which faces can be seen (pure) ----
// The cube is centred on the camera with half-size 's'. 'rot' is the 3x3 rotation of the view matrix (column-major 4x4 with the
// translation ignored: rot[col*4 + row]). A face is skipped only when all four of its corners lie outside the SAME frustum plane
// (left/right/top/bottom) or all behind the camera, which is conservative: it never hides a face that is on screen.
// fovYdeg is the vertical field of view, aspect = width / height. 'margin' widens the frustum a little (1.05 = 5%).
struct FaceVisibility { bool visible[6]; int count = 0; };

inline FaceVisibility visibleFaces(const float* view, float fovYdeg, float aspect, float margin = 1.05f) {
    const float h = 1.0f;   // the cube is scale-free: corners are +-1
    const float faces[6][4][3] = {   // same corner tables as the draw code, for a unit cube
        {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}},   // front  (-Z)
        {{h, -h, h}, {-h, -h, h}, {-h, h, h}, {h, h, h}},       // back   (+Z)
        {{-h, -h, h}, {-h, -h, -h}, {-h, h, -h}, {-h, h, h}},   // left   (-X)
        {{h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h}},       // right  (+X)
        {{-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h}},       // top    (+Y)
        {{-h, -h, h}, {h, -h, h}, {h, -h, -h}, {-h, -h, -h}},   // bottom (-Y)
    };
    float tanV = std::tan(fovYdeg * 0.5f * 3.14159265f / 180.0f) * margin;
    float tanH = tanV * (aspect > 0 ? aspect : 1.0f);
    FaceVisibility out;
    for (int f = 0; f < 6; f++) {
        bool allLeft = true, allRight = true, allBottom = true, allTop = true, allBehind = true;
        for (int c = 0; c < 4; c++) {
            const float* p = faces[f][c];
            float x = view[0] * p[0] + view[4] * p[1] + view[8] * p[2];
            float y = view[1] * p[0] + view[5] * p[1] + view[9] * p[2];
            float d = -(view[2] * p[0] + view[6] * p[1] + view[10] * p[2]);   // distance in front of the camera
            allBehind = allBehind && d <= 0;
            allLeft = allLeft && x < -d * tanH;
            allRight = allRight && x > d * tanH;
            allBottom = allBottom && y < -d * tanV;
            allTop = allTop && y > d * tanV;
        }
        out.visible[f] = !(allBehind || allLeft || allRight || allBottom || allTop);
        out.count += out.visible[f];
    }
    return out;
}

} // namespace world
