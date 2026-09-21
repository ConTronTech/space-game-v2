#pragma once
// Pure lookup structure for rendered text (no GL, no SDL): values are cached per (font size, string).
// Lookups take the caller's std::string directly, so a cache HIT allocates nothing (the old "<size>:<string>" key built a new string on
// every text call, every frame). Entries remember the frame they were last used, so a full cache drops only what has gone stale instead
// of flushing everything at once (a flush re-renders every visible string in one frame: a visible hitch). Unit-tested in test_perf.cpp.
#include <string>
#include <unordered_map>

namespace core {

template <class V>
class TextCache {
public:
    V* find(int size, const std::string& s, unsigned long frame) {
        auto b = bySize_.find(size);
        if (b == bySize_.end()) return nullptr;
        auto it = b->second.find(s);
        if (it == b->second.end()) return nullptr;
        it->second.used = frame;
        return &it->second.value;
    }
    // Adds (or replaces) an entry; the returned pointer stays valid until that entry is evicted or the cache is cleared.
    V* insert(int size, const std::string& s, V value, unsigned long frame) {
        auto& m = bySize_[size];
        auto it = m.find(s);
        if (it == m.end()) { it = m.emplace(s, Entry{std::move(value), frame}).first; count_++; }
        else { it->second.value = std::move(value); it->second.used = frame; }
        return &it->second.value;
    }
    size_t size() const { return count_; }

    // Removes entries not used for more than 'age' frames; release(value) is called for each (e.g. glDeleteTextures). Returns how many.
    template <class F> size_t evictStale(unsigned long frame, unsigned long age, F&& release) {
        size_t n = 0;
        for (auto& [sz, m] : bySize_)
            for (auto it = m.begin(); it != m.end();) {
                if (frame - it->second.used > age) { release(it->second.value); it = m.erase(it); n++; count_--; }
                else ++it;
            }
        return n;
    }
    template <class F> void clear(F&& release) {
        for (auto& [sz, m] : bySize_) for (auto& [k, e] : m) release(e.value);
        bySize_.clear();
        count_ = 0;
    }

private:
    struct Entry { V value; unsigned long used; };
    std::unordered_map<int, std::unordered_map<std::string, Entry>> bySize_;
    size_t count_ = 0;
};

} // namespace core
