#pragma once
// ui/controller_setup service: the pause menu's Settings > Controllers row opens the screen through it (looked up per use, so no dependency).
//     if (auto* cs = eng.services.get<ui::IControllerSetup>()) cs->open();
namespace ui {

class IControllerSetup {
public:
    virtual ~IControllerSetup() = default;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
};

} // namespace ui
