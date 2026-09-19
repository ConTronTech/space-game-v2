#pragma once
// Service registry: how modules find each other without hard-wiring.
// A module publishes itself (or an interface) in init():
//     engine.services.provide<IInput>(this);
// Another module looks it up:
//     auto* input = engine.services.get<IInput>();   // nullptr if absent
//     auto& input = engine.services.require<IInput>(); // throws if absent
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

namespace engine {

class Services {
public:
    template <class T> void provide(T* svc) { map_[typeid(T)] = svc; }
    template <class T> void withdraw() { map_.erase(typeid(T)); }
    template <class T> T* get() const {
        auto it = map_.find(typeid(T));
        return it == map_.end() ? nullptr : static_cast<T*>(it->second);
    }
    template <class T> T& require() const {
        T* p = get<T>();
        if (!p) throw std::runtime_error(std::string("missing service: ") + typeid(T).name());
        return *p;
    }

private:
    std::unordered_map<std::type_index, void*> map_;
};

} // namespace engine
