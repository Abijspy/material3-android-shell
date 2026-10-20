// The lock screen editor: the lock screen itself at full size, with its grid
// laid over it. Click a cell to add a widget there, or a widget to move or
// remove it. Everything saves at once (lock.grid), and the greeter follows.
#include "core/Config.h"
#include "core/EventLoop.h"
#include "render/Images.h"
#include "services/Wallpaper.h"
#include "shell/LockLayout.h"
#include "shell/LockView.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// Wallpaper and scrim, painted like the lock screen's own background
class StageBackground : public Node {
protected:
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        const Radii radius(24);
        r.rect(b, Theme::get().color("surface"), radius);
        const std::string wp = Wallpaper::get().lockPath();
        if (!wp.empty()) {
            const int w = static_cast<int>(std::lround(b.w * r.scale())), h = static_cast<int>(std::lround(b.h * r.scale()));
            if (auto* t = Images::get().picture(wp, w, h, true, [this] { requestRedraw(); })) {
                r.pushClip(b);
                const float s = std::max(b.w / static_cast<float>(t->width), b.h / static_cast<float>(t->height));
                const float dw = static_cast<float>(t->width) * s, dh = static_cast<float>(t->height) * s;
                r.image(t->id, {b.x + (b.w - dw) / 2, b.y + (b.h - dh) / 2, dw, dh});
                r.popClip();
            }
        }
        r.rect(b, Theme::get().color("surface").withAlpha(0.55f), radius);
    }
};

// A cell: nothing it holds bleeds into its neighbours
class ClippedButton : public Button {
public:
    ClippedButton() : Button(Direction::Column) {}
    bool holdsWidget = false; // not the "Add" hint

protected:
    bool clipsChildren() const override { return true; }
};

// The stage: background behind everything, the 3 × 4 cells above the login
// area, and the login preview (avatar and password) along the bottom. Rows
// size to their widgets as on the real lock screen; empty ones keep room to
// click into.
class Stage : public Node {
public:
    float loginHeight = 84, gap = 10, pad = 20, minRow = 72;

protected:
    // Takes what the column leaves it (it grows), never more
    SizeF doMeasure(SizeF available) override { return {available.w, 0}; }
    void doArrange() override {
        const auto& r = rect();
        const auto& kids = children();
        if (kids.size() < 3) return;
        kids[0]->arrange(r);
        kids[1]->arrange(r);
        const float gw = r.w - pad * 2;
        const float cw = (gw - gap * (lockgrid::kCols - 1)) / lockgrid::kCols;
        const auto& cells = kids[1]->children();
        // Too tall for the stage: the tallest row's widgets shrink by what it
        // overflows (a big stacked clock) rather than being cut off by their
        // cells. Natural sizes are the measured ones over the size shown.
        const float room = r.h - pad - loginHeight;
        const float gaps = gap * (lockgrid::kRows - 1);
        std::vector<float> natural(lockgrid::kRows, minRow);
        for (size_t i = 0; i < cells.size(); ++i) {
            auto& n = natural[i / lockgrid::kCols];
            n = std::max(n, cells[i]->measure({cw, 1e6f}).h / cells[i]->textMul);
        }
        float naturalTotal = gaps;
        for (float h : natural) naturalTotal += h;
        const size_t tallest = static_cast<size_t>(std::ranges::max_element(natural) - natural.begin());
        const float fit = naturalTotal > room ? std::clamp((natural[tallest] - (naturalTotal - room)) / natural[tallest], 0.4f, 1.0f) : 1;
        bool changed = false;
        for (size_t i = 0; i < cells.size(); ++i) {
            const auto* cell = dynamic_cast<const ClippedButton*>(cells[i].get());
            const float want = i / lockgrid::kCols == tallest && cell && cell->holdsWidget ? fit : 1;
            if (std::abs(cells[i]->textMul - want) > 0.01f) {
                cells[i]->textMul = want;
                changed = true;
            }
        }
        if (changed) requestLayout(); // measured again at the new size
        std::vector<float> rows(lockgrid::kRows, minRow);
        for (size_t i = 0; i < cells.size(); ++i)
            rows[i / lockgrid::kCols] = std::max(rows[i / lockgrid::kCols], cells[i]->measure({cw, 1e6f}).h);
        // Whatever still doesn't fit shares what there is
        float total = gaps;
        for (float h : rows) total += h;
        const float k = total > room ? (room - gap * (lockgrid::kRows - 1)) / (total - gap * (lockgrid::kRows - 1)) : 1;
        float y = r.y + pad;
        for (size_t i = 0; i < cells.size(); ++i) {
            const size_t c = i % lockgrid::kCols, row = i / lockgrid::kCols;
            if (c == 0 && row > 0) y += rows[row - 1] * k + gap;
            cells[i]->arrange({r.x + pad + (cw + gap) * static_cast<float>(c), y, cw, rows[row] * k});
        }
        const SizeF s = kids[2]->measure({r.w, loginHeight});
        kids[2]->arrange({r.x + (r.w - s.w) / 2, r.bottom() - loginHeight + (loginHeight - s.h) / 2, s.w, s.h});
    }
    bool clipsChildren() const override { return true; }
};

// A board of cells, arranged by Stage
class Cells : public Node {
protected:
    SizeF doMeasure(SizeF available) override { return available; }
    void doArrange() override {}
};

} // namespace

void lockEditor(Flex* card, Popup& popup) {
    card->setPadding(Insets(16))->setSpacing(12);
    auto* live = card->add<Live>();
    live->setSpacing(12);
    live->grow = true;
    auto sel = std::make_shared<std::pair<int, int>>(-1, -1);

    live->sync = [live, sel, &popup] {
        live->clearChildren();
        using namespace lockgrid;
        const auto items = load();
        auto at = [&](int c, int r) -> const Item* {
            for (const auto& it : items)
                if (it.col == c && it.row == r) return &it;
            return nullptr;
        };

        // Title and the editor's own buttons
        auto* top = live->add<Flex>(Flex::Direction::Row);
        top->setSpacing(10)->setPadding({8, 0});
        auto* badge = top->add<Flex>(Flex::Direction::Row);
        badge->setJustify(Align::Center)->setRadius(Radii(14))->setBackground("primary_container", false);
        badge->fixedWidth = badge->fixedHeight = 44;
        badge->add<Icon>("lock_person", 24)->setFill(true)->setColor("on_primary_container", false);
        auto* titles = top->add<Flex>(Flex::Direction::Column);
        titles->setAlignItems(Align::Start)->setSpacing(1);
        titles->grow = true;
        titles->add<Label>("Edit lock screen")->setSize(22)->setWeight(600);
        titles->add<Label>("The login screen shows the same layout")->setSize(13)->setColor("on_surface_variant", false);
        auto button = [top](const char* icon, const char* label, bool primary, std::function<void()> fn) {
            auto* b = top->add<Button>();
            pill(b->setBackground(primary ? ColorRef("primary") : ColorRef("on_surface", 0.1f), false))->setPadding({16, 9})->setSpacing(6);
            b->pressedRadius = 10;
            b->add<Icon>(icon, 18)->setColor(primary ? "on_primary" : "on_surface", false);
            b->add<Label>(label)->setSize(14)->setWeight(500)->setColor(primary ? "on_primary" : "on_surface", false);
            b->onClicked = [fn = std::move(fn)](uint32_t btn) {
                if (btn == BTN_LEFT) fn();
            };
        };
        button("restart_alt", "Default", false, [sel] {
            *sel = {-1, -1};
            Config::get().set("/lock/grid", nullptr);
            Config::get().set("/lock/widgets", nullptr);
        });
        button("tune", "Clock style", false, [&popup] {
            popup.close();
            if (open) open("settings lockscreen");
        });
        button("lock", "Try it", false, [&popup] {
            popup.close();
            if (lock) lock();
        });
        button("check", "Done", true, [&popup] { popup.close(); });

        // What the selection allows
        const int c = sel->first, r = sel->second;
        // A row of controls; a column when it lists widgets to add
        const bool adding = c >= 0 && !at(c, r);
        auto* actions = live->add<Flex>(adding ? Flex::Direction::Column : Flex::Direction::Row);
        actions->setSpacing(6)->setPadding({18, 8, 8, 8})->setRadius(Radii(28))->setBackground(ColorRef("on_surface", 0.1f), false);
        actions->minHeight = 56;
        if (c < 0) {
            actions->add<Icon>("touch_app", 20)->setColor("on_surface_variant", false);
            actions->add<Label>("Click a cell to add a widget, or a widget to move or remove it")->setSize(14)->setColor("on_surface_variant", false);
        } else if (const Item* it = at(c, r)) {
            const auto* d = find(it->id);
            actions->add<Icon>(d->icon, 20)->setColor("primary", false);
            auto* name = actions->add<Label>(std::format("{}  ·  {}", d->name, cellName(c, r)));
            name->setSize(14)->setWeight(600);
            name->grow = true;
            auto mv = [sel, c, r](int dc, int dr) {
                const int nc = c + dc, nr = r + dr;
                if (nc < 0 || nc >= kCols || nr < 0 || nr >= kRows) return;
                *sel = {nc, nr};
                lockgrid::move(c, r, dc, dr);
            };
            if (c > 0) iconButton(actions, "arrow_back", [mv] { mv(-1, 0); });
            if (r > 0) iconButton(actions, "arrow_upward", [mv] { mv(0, -1); });
            if (r < kRows - 1) iconButton(actions, "arrow_downward", [mv] { mv(0, 1); });
            if (c < kCols - 1) iconButton(actions, "arrow_forward", [mv] { mv(1, 0); });
            iconButton(actions, "delete", [sel, c, r] {
                *sel = {-1, -1};
                lockgrid::remove(c, r);
            });
        } else {
            // As Android's editors add: each free widget an icon chip with
            // its name under it
            actions->setAlignItems(Align::Start)->setPadding({18, 10});
            actions->add<Label>(std::format("Add to the {}", cellName(c, r)))->setSize(14)->setWeight(600);
            auto* avail = actions->add<Flow>();
            avail->spacing = 6;
            size_t free = 0;
            for (const auto& d : widgets()) {
                if (std::ranges::any_of(items, [&](const Item& o) { return o.id == d.id; })) continue;
                ++free;
                auto* item = avail->add<Button>(Flex::Direction::Column);
                item->setPadding({4, 6})->setSpacing(4)->setRadius(Radii(18));
                item->pressedRadius = 12;
                item->fixedWidth = 96;
                auto* icon = item->add<Flex>(Flex::Direction::Row);
                pill(icon->setBackground(ColorRef("on_surface", 0.1f), false))->setJustify(Align::Center);
                icon->fixedWidth = 60;
                icon->fixedHeight = 44;
                icon->alignSelf = Align::Center;
                icon->add<Icon>(d.icon, 22)->setColor("on_surface", false);
                auto* label = item->add<Label>(d.name);
                label->setSize(12)->setMaxWidth(90)->setMaxLines(2)->setCentered(true)->setColor("on_surface_variant", false);
                label->alignSelf = Align::Center;
                const std::string id = d.id;
                item->onClicked = [id, c, r](uint32_t button) {
                    if (button == BTN_LEFT) lockgrid::put(id, c, r);
                };
            }
            if (free == 0) caption(actions, "Every widget is placed; remove one first");
        }

        // The lock screen, at full size
        auto* stage = live->add<Stage>();
        stage->grow = true;
        stage->add<StageBackground>();
        auto* cells = stage->add<Cells>();
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < kCols; ++col) {
                const Item* it = at(col, row);
                const bool selected = sel->first == col && sel->second == row;
                auto* cell = cells->add<ClippedButton>();
                cell->setRadius(Radii(22))->setAlignItems(col == 0 ? Align::Start : col == 1 ? Align::Center : Align::End);
                cell->setJustify(Align::Center)->setPadding({16, 8});
                cell->setBorder(selected ? 3 : 1.5f, selected ? ColorRef("primary") : ColorRef("outline", 0.55f));
                cell->setBackground(selected ? ColorRef("primary", 0.14f) : ColorRef::none(), false);
                if (it) {
                    // The stage is about 3/4 of the screen's height: the clock follows
                    addLockWidget(cell, it->id, false, col, 0.75f);
                    cell->holdsWidget = true;
                } else {
                    auto* hint = cell->add<Flex>(Flex::Direction::Row);
                    hint->setSpacing(6);
                    hint->alignSelf = Align::Center;
                    hint->add<Icon>("add", 22)->setColor(selected ? "primary" : "on_surface_variant", false);
                    hint->add<Label>("Add")->setSize(14)->setColor(selected ? "primary" : "on_surface_variant", false);
                }
                cell->onClicked = [sel, live, col, row](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    *sel = sel->first == col && sel->second == row ? std::pair{-1, -1} : std::pair{col, row};
                    live->syncSoon();
                };
            }
        // Login area preview: what stays fixed under the grid
        auto* login = stage->add<Flex>(Flex::Direction::Row);
        login->setSpacing(14)->setOpacity(0.7f, false);
        auto* who = login->add<Flex>(Flex::Direction::Row);
        who->setSpacing(12);
        auto* avatar = who->add<Flex>(Flex::Direction::Row);
        pill(avatar->setBackground("primary_container", false))->setJustify(Align::Center);
        avatar->fixedWidth = avatar->fixedHeight = 48;
        avatar->add<Icon>("person", 24)->setColor("on_primary_container", false);
        who->add<Label>("You")->setSize(16)->setColor("on_surface", false);
        auto* field = login->add<Flex>(Flex::Direction::Row);
        pill(field->setBackground("surface_container_highest", false))->setPadding({20, 12});
        field->fixedWidth = 360;
        field->add<Label>("Password · stays here")->setSize(14)->setColor("on_surface_variant", false);
    };
    live->conns.push_back(Config::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}

} // namespace m3::shell::panels
