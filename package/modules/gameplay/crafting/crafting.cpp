// gameplay/crafting - turn mined ore into items and use them (fuel cells, repair kits, shields...). Recipes and items are data; effects go through
// ship::IShip only. Rules in crafting_rules.h, format and refusal reasons in docs/CRAFTING.md. Also adds the CRAFTING tab to the game menu.
#include <algorithm>
#include <map>
#include <cstdio>
#include "combat/weapons/weapons_api.h"
#include "core/audio/audio_api.h"
#include "core/data_registry/data_api.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/crafting/crafting_api.h"
#include "gameplay/crafting/crafting_data.h"
#include "gameplay/crafting/crafting_rules.h"
#include "gameplay/inventory/inventory_api.h"
#include "ship/docking/docking_api.h"
#include "ship/ship_core/ship_api.h"
#include "ui/game_menu/game_menu_api.h"

class Crafting : public engine::Module, public gameplay::ICrafting {
public:
    const char* name() const override { return "gameplay/crafting"; }
    std::vector<std::string> dependencies() const override { return {"gameplay/inventory", "core/data_registry"}; }
    std::vector<std::string> optionalDependencies() const override { return {"ui/game_menu", "ship/ship_core", "ship/fake_ship", "ship/docking", "core/audio"}; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        requireDock_ = eng.config.get("crafting.require_dock", true, "true = crafting only works while docked at a station (a workbench); false = craft anywhere (user decision 2026-09-21: default true, a settable option either way)");
        data_ = &eng.services.require<core::IData>();
        for (auto& id : data_->ids("recipes")) recipes_.push_back(gameplay::recipeFromJson(id, data_->get("recipes", id)));
        for (auto& id : data_->ids("items")) effects_[id] = gameplay::effectFromJson(data_->get("items", id));
        for (auto& r : recipes_) if (!data_->has("items", r.result)) LOG_W("crafting", "recipe '%s' makes unknown item '%s'", r.id.c_str(), r.result.c_str());
        eng.services.provide<gameplay::ICrafting>(this);
        if (auto* menu = eng.services.get<ui::IGameMenu>()) {
            menu_ = menu;
            menu->addTab("CRAFTING", 20, [this](core::UIHandler& ui, float x, float y, float w, float h) { tab(ui, x, y, w, h); });
        }
        LOG_I("crafting", "%zu recipes, %zu items%s", recipes_.size(), effects_.size(), requireDock_ ? ", crafting needs a docked station" : "");
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (menu_) menu_->removeTab("CRAFTING");
        eng.services.withdraw<gameplay::ICrafting>();
    }

    // ---- gameplay::ICrafting ----
    void recipes(std::vector<gameplay::RecipeInfo>& out) const override {
        out.clear();
        for (auto& r : recipes_) {
            gameplay::RecipeInfo info;
            info.id = r.id; info.name = r.name; info.result = r.result;
            for (auto& ing : r.ingredients) info.ingredients.push_back({ing.id, nameOf(ing.id), ing.need, inv() ? inv()->count(ing.id) : 0});
            info.craftable = canCraft(r.id, info.reason);
            out.push_back(std::move(info));
        }
    }

    bool canCraft(const std::string& id, std::string& reason) const override {
        const gameplay::Recipe* r = find(id);
        if (!r) { reason = "unknown recipe"; return false; }
        auto* i = inv();
        if (!i) { reason = "no cargo hold"; return false; }
        auto* dock = eng_->services.get<ship::IDocking>();
        auto d = gameplay::decideCraft(*r, [&](const std::string& k) { return i->count(k); }, [&](const std::string& k) { return volumeOf(k); },
                                       i->free(r->result), volumeOf(r->result), requireDock_, dock && dock->docked(), [&](const std::string& k) { return !i->isResource(k); });
        reason = d.reason;
        return d.ok;
    }

    bool craft(const std::string& id, std::string& reason) override {
        const gameplay::Recipe* r = find(id);
        bool ok = canCraft(id, reason);
        if (ok) {
            auto* i = inv();
            // the check said everything is there and fits: consume, then add. If a remove fails anyway (a listener changed the hold) put things back.
            std::vector<gameplay::Ingredient> taken;
            for (auto& ing : r->ingredients) {
                if (!i->remove(ing.id, ing.need)) { ok = false; break; }
                taken.push_back(ing);
            }
            if (ok && i->add(r->result, 1) < 1) ok = false;
            if (!ok) { for (auto& t : taken) i->add(t.id, t.need); reason = "cargo changed, try again"; }
        }
        setMessage(ok ? "Crafted " + (r ? r->name : id) : "Cannot craft " + (r ? r->name : id) + ": " + reason, ok);
        LOG_I("crafting", "%s", message_.c_str());
        if (ok) confirm();
        eng_->events.emit(gameplay::CraftResult{id, ok, reason});
        return ok;
    }

    bool usable(const std::string& itemId) const override { auto it = effects_.find(itemId); return it != effects_.end() && it->second.any(); }

    bool use(const std::string& itemId, std::string& reason) override {
        auto* i = inv();
        auto it = effects_.find(itemId);
        std::string nm = nameOf(itemId);
        auto fail = [&](const std::string& why) { reason = why; setMessage("Cannot use " + nm + ": " + why, false); LOG_I("crafting", "%s", message_.c_str()); return false; };
        if (!i || i->count(itemId) < 1) return fail("not in cargo");
        if (it == effects_.end() || !it->second.any()) return fail("this item cannot be used");
        auto* ship = eng_->services.get<ship::IShip>();
        if (!ship) return fail("no ship");
        const auto& st = ship->status();
        gameplay::ShipState ss;
        ss.alive = st.alive; ss.hp = st.hp; ss.maxHp = st.maxHp; ss.warpFuel = st.warpFuel; ss.maxWarpFuel = st.maxWarpFuel; ss.shieldInstalled = st.shieldInstalled;
        auto* ammo = eng_->services.get<combat::IAmmo>();
        if (ammo) { ss.missiles = ammo->missiles(); ss.maxMissiles = ammo->maxMissiles(); }
        ss.oreScanner = i->hasPerk("ore_scanner");
        gameplay::UsePlan plan = gameplay::decideUse(it->second, ss);
        if (!plan.ok) return fail(plan.reason);                                 // refused: the item stays in the hold
        float fuelBefore = st.warpFuel, hpBefore = st.hp;
        if (plan.addFuel > 0) ship->addWarpFuel(plan.addFuel);
        if (plan.heal > 0) ship->heal(plan.heal);
        if (plan.addMaxHp > 0) ship->addMaxHp(plan.addMaxHp);
        if (plan.installShield) ship->installShield(true);
        if (plan.addMissiles > 0 && ammo && ammo->addMissiles(plan.addMissiles) <= 0 && !(plan.addFuel > 0 || plan.heal > 0 || plan.addMaxHp > 0 || plan.installShield))
            return fail("missile rack full");
        if (plan.setOreScanner) i->addPerk("ore_scanner");
        i->remove(itemId, 1);                                                    // consumed only after the effect was applied
        reason = plan.reason;
        setMessage("Used " + nm + ": " + plan.reason, true);
        LOG_I("crafting", "%s (warp fuel %.1f -> %.1f, hull %.1f -> %.1f)", message_.c_str(), fuelBefore, ship->status().warpFuel, hpBefore, ship->status().hp);
        confirm();
        eng_->events.emit(gameplay::ItemUsed{itemId});
        return true;
    }

    bool requiresDock() const override { return requireDock_; }
    const std::string& message() const override { return message_; }
    bool messageOk() const override { return messageOk_; }
    double messageAge() const override { return eng_ ? eng_->time() - messageTime_ : 1e9; }

private:
    gameplay::IInventory* inv() const { return eng_->services.get<gameplay::IInventory>(); }
    const gameplay::Recipe* find(const std::string& id) const { for (auto& r : recipes_) if (r.id == id) return &r; return nullptr; }
    float volumeOf(const std::string& id) const { return data_->has("items", id) ? (float)data_->get("items", id)["volume"].num(1.0) : 1.0f; }
    std::string nameOf(const std::string& id) const {
        for (const char* cat : {"items", "ores"}) if (data_->has(cat, id)) return data_->get(cat, id)["name"].str(id);
        return id;
    }
    void setMessage(const std::string& m, bool ok) { message_ = m; messageOk_ = ok; messageTime_ = eng_->time(); }
    void confirm() { if (auto* a = eng_->services.get<core::IAudio>()) if (a->hasSound("ui_confirm")) a->play("ui_confirm", 0.6f); }

    // ---- the CRAFTING tab ----
    void tab(core::UIHandler& ui, float x, float y, float w, float h) {
        auto* dock = eng_->services.get<ship::IDocking>();
        bool needDock = requireDock_ && !(dock && dock->docked());
        std::vector<gameplay::RecipeInfo> list;
        recipes(list);
        if (needDock) ui.text(x, y, "Dock at a station to craft", 15, core::Color{1.0f, 0.75f, 0.35f, 1});
        else ui.text(x, y, "Turn ore into fuel, repairs and upgrades", 15, ui.theme.textDim);
        float ry = y + 26;
        const float rowH = 40;
        for (auto& r : list) {
            if (ry + rowH > y + h - 26) break;
            ui.glass(x, ry, w, rowH, 0.5f, false, 8);
            ui.text(x + 12, ry + 3, r.name, 15, ui.theme.text);
            float ix = x + 12;
            for (auto& ing : r.ingredients) {                                   // "iron 5/5" green when there is enough, red when not
                char b[64]; std::snprintf(b, sizeof b, "%s %d/%d", ing.name.c_str(), ing.have, ing.need);
                bool enough = ing.have >= ing.need;
                ui.text(ix, ry + 21, b, 13, enough ? core::Color{0.4f, 0.95f, 0.5f, 1} : core::Color{1.0f, 0.5f, 0.4f, 1});
                ix += (float)ui.textWidth(b, 13) + 14;
            }
            float bx = x + w - 100, by = ry + 4, bw = 92, bh = rowH - 8;
            if (r.craftable) {
                if (ui.button("CRAFT", bx, by, bw, bh, false)) { std::string why; craft(r.id, why); }
            } else {
                ui.glass(bx, by, bw, bh, 0.3f, false, 10);                       // greyed out: not clickable
                ui.textCentered(bx + bw / 2, by + (bh - 18 * 1.3f) / 2, "CRAFT", 18, core::Color{0.4f, 0.45f, 0.52f, 1});
                std::string why = r.reason;
                ui.text(bx - 12 - (float)ui.textWidth(why, 12), ry + 5, why, 12, core::Color{1.0f, 0.6f, 0.45f, 1});
            }
            ry += rowH + 3;
        }
        if (!message_.empty() && messageAge() < 8.0)
            ui.text(x, y + h - 22, message_, 14, messageOk_ ? core::Color{0.4f, 0.95f, 0.5f, 1} : core::Color{1.0f, 0.5f, 0.4f, 1});
    }

    engine::Engine* eng_ = nullptr;
    core::IData* data_ = nullptr;
    ui::IGameMenu* menu_ = nullptr;
    bool requireDock_ = false, messageOk_ = false;
    double messageTime_ = -1e9;
    std::string message_;
    std::vector<gameplay::Recipe> recipes_;
    std::map<std::string, gameplay::Effect> effects_;
};

REGISTER_MODULE(Crafting);
