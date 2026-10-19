#pragma once

// Building blocks shared by the Settings pages (Settings.cpp and the
// Hyprland pages): M3 Expressive segmented lists, rows and button groups.
#include "shell/PanelKit.h"
#include "ui/Controls.h"
#include "ui/Node.h"
#include "ui/Theme.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace m3::shell::panels {

using namespace m3::ui;

// M3 Expressive: large outer corners, tight inner ones
inline constexpr float kOuter = 28, kInner = 6;

// The rows of one group. Their backgrounds are painted here, shaped by their
// place in the group, so rows added or hidden later reshape the stack.
class Segments : public Flex {
public:
    Segments() : Flex(Direction::Column) { setAlignItems(Align::Stretch)->setSpacing(2); }

protected:
    void doArrange() override {
        Flex::doArrange();
        const auto rows = shownRows();
        for (size_t i = 0; i < rows.size(); ++i) {
            const Radii want = shape(i, rows.size());
            const Radii& has = rows[i]->radius();
            // Hover and press layers follow the segment's corners
            if (has.tl != want.tl || has.bl != want.bl) rows[i]->setRadius(want);
        }
    }
    void doPaint(Renderer& r) override {
        const Color bg = ColorRef("surface_container_high", std::min(1.0f, Theme::get().panelOpacity() + 0.06f)).resolve();
        const float k = Theme::get().roundness();
        const auto rows = shownRows();
        for (size_t i = 0; i < rows.size(); ++i) {
            const Radii s = shape(i, rows.size());
            const float half = std::min(rows[i]->rect().w, rows[i]->rect().h) / 2;
            auto f = [&](float v) { return std::min(v * k, half); };
            r.rect(rows[i]->rect(), bg, {f(s.tl), f(s.tr), f(s.br), f(s.bl)});
        }
    }

private:
    std::vector<Flex*> shownRows() const {
        std::vector<Flex*> out;
        for (const auto& c : children())
            if (c->shown())
                if (auto* f = dynamic_cast<Flex*>(c.get())) out.push_back(f);
        return out;
    }
    static Radii shape(size_t i, size_t n) {
        const float top = i == 0 ? kOuter : kInner, bottom = i + 1 == n ? kOuter : kInner;
        return {top, top, bottom, bottom};
    }
};



// A titled group of rows
Flex* group(Node* page, const std::string& title, const std::string& subtitle = {});
// A group holding one block of controls on a single surface (popup
// layouts reused inside Settings)
Flex* panelGroup(Node* page, const std::string& title, const std::string& subtitle = {});

// One row: icon, title and subtitle, and a trailing slot for the control
struct Row {
    Flex* row;
    Label* title;
    Label* subtitle;
};
Row row(Node* group, const std::string& icon, const std::string& title, const std::string& subtitle = {});
Button* pillButton(Node* parent, const std::string& label, bool primary, std::function<void()> fn, const std::string& icon = {});

// M3 Expressive connected button group: segments a hairline apart with tight
// inner corners; the selected one turns primary and rounds off completely.
template <typename T>
Flex* connectedGroup(Node* parent, T current, std::vector<std::pair<T, std::string>> options, std::function<void(T)> pick) {
    auto* chips = parent->add<Flex>(Flex::Direction::Row);
    chips->setSpacing(2);
    auto buttons = std::make_shared<std::vector<std::pair<T, Button*>>>();
    auto paint = [buttons](const T& selected, bool animate) {
        const size_t n = buttons->size();
        for (size_t i = 0; i < n; ++i) {
            auto& [val, btn] = (*buttons)[i];
            const bool on = val == selected;
            const float l = on || i == 0 ? 100 : 8, rr = on || i + 1 == n ? 100 : 8;
            btn->setRadius({l, rr, rr, l});
            btn->setBackground(on ? ColorRef("primary") : ColorRef("surface_container_highest"), animate);
            for (auto& c : btn->children())
                if (auto* lab = dynamic_cast<Label*>(c.get())) lab->setColor(on ? "on_primary" : "on_surface", animate);
        }
    };
    for (const auto& [value, label] : options) {
        auto* b = chips->add<Button>();
        b->setPadding({14, 9})->setSpacing(6);
        b->pressedRadius = 4;
        b->add<Label>(label)->setSize(13)->setWeight(500);
        buttons->push_back({value, b});
        b->onClicked = [=, v = value](uint32_t) {
            paint(v, true);
            if (pick) pick(v);
        };
    }
    paint(current, false);
    return chips;
}

} // namespace m3::shell::panels
