#pragma once
// Typed publish/subscribe. Any struct can be an event.
//   bus.subscribe<MyEvent>([](const MyEvent& e){ ... });
//   bus.emit(MyEvent{...});
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace engine {

class EventBus {
public:
    template <class E>
    void subscribe(std::function<void(const E&)> fn) {
        handlers_[typeid(E)].push_back([fn = std::move(fn)](const void* e) {
            fn(*static_cast<const E*>(e));
        });
    }

    template <class E>
    void emit(const E& e) const {
        auto it = handlers_.find(typeid(E));
        if (it == handlers_.end()) return;
        for (auto& h : it->second) h(&e);
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const void*)>>> handlers_;
};

} // namespace engine
