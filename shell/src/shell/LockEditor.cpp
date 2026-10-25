// The lock screen editor: the lock screen itself, its grid laid over it, and
// a side sheet that follows the selection: the widget library (grouped,
// each with Add) when nothing is picked; "Add to …" for an empty cell; a
// widget's details, a pad to move it, Replace and Remove for a placed one.
// Widgets hidden right now (music with nothing playing) show as a labelled
// placeholder, never as a blank cell. Everything saves at once (lock.grid),
// and the greeter follows.
#include "core/Config.h"
#include "core/EventLoop.h"
#include "render/Images.h"
#include "services/Wallpaper.h"
#include "shell/LockLayout.h"
#include "shell/LockView.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/SettingsKit.h"
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

namespace {

// A tonal bubble with an icon (the library's and the details' badge)
Flex* bubble(Node* parent, const std::string& icon, float size, bool primary) {
    auto* b = parent->add<Flex>(Flex::Direction::Row);
    b->setJustify(Align::Center)->setRadius(Radii(size * 0.32f));
    b->setBackground(primary ? ColorRef("primary_container") : ColorRef("on_surface", 0.1f), false);
    b->fixedWidth = b->fixedHeight = size;
    b->add<Icon>(icon, size * 0.5f)->setColor(primary ? "on_primary_container" : "on_surface", false);
    return b;
}

// The first free cell, reading order (top left first)
std::pair<int, int> firstFree(const std::vector<lockgrid::Item>& items) {
    for (int r = 0; r < lockgrid::kRows; ++r)
        for (int c = 0; c < lockgrid::kCols; ++c)
            if (std::ranges::none_of(items, [&](const lockgrid::Item& o) { return o.col == c && o.row == r; })) return {c, r};
    return {-1, -1};
}

} // namespace

void lockEditor(Flex* card, Popup& popup) {
    card->setPadding(Insets(16))->setSpacing(12);
    auto* live = card->add<Live>();
    live->setSpacing(12);
    live->grow = true;
    struct State {
        int col = -1, row = -1;  // the selected cell
        bool replacing = false;  // choosing a widget for a filled cell
    };
    auto st = std::make_shared<State>();

    live->sync = [live, st, &popup] {
        live->clearChildren();
        using namespace lockgrid;
        const auto items = load();
        auto at = [&](int c, int r) -> const Item* {
            for (const auto& it : items)
                if (it.col == c && it.row == r) return &it;
            return nullptr;
        };
        auto select = [st, live](int c, int r) {
            st->col = c;
            st->row = r;
            st->replacing = false;
            live->syncSoon();
        };

        // Title and the editor's own buttons
        auto* top = live->add<Flex>(Flex::Direction::Row);
        top->setSpacing(10)->setPadding({8, 0});
        bubble(top, "lock_person", 44, true);
        auto* titles = top->add<Flex>(Flex::Direction::Column);
        titles->setAlignItems(Align::Start)->setSpacing(1);
        titles->grow = true;
        titles->add<Label>("Edit lock screen")->setSize(22)->setWeight(600);
        titles->add<Label>("Tap a cell to add or change its widget · the login screen shows the same layout")
            ->setSize(13)
            ->setColor("on_surface_variant", false);
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
        button("restart_alt", "Default", false, [st] {
            *st = {};
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

        auto* body = live->add<Flex>(Flex::Direction::Row);
        body->setSpacing(14)->setAlignItems(Align::Stretch);
        body->grow = true;

        // ── The lock screen, its grid over it ──
        auto* stage = body->add<Stage>();
        stage->grow = true;
        stage->add<StageBackground>();
        auto* cells = stage->add<Cells>();
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < kCols; ++col) {
                const Item* it = at(col, row);
                const bool selected = st->col == col && st->row == row;
                auto* cell = cells->add<ClippedButton>();
                cell->setRadius(Radii(22))->setAlignItems(col == 0 ? Align::Start : col == 1 ? Align::Center : Align::End);
                cell->setJustify(Align::Center)->setPadding({16, 8});
                cell->setBorder(selected ? 3 : 1.5f, selected ? ColorRef("primary") : ColorRef("outline", 0.45f));
                cell->setBackground(selected ? ColorRef("primary", 0.14f) : ColorRef::none(), false);
                if (it) {
                    // The stage is about 3/4 of the screen's height: the clock follows
                    Node* w = addLockWidget(cell, it->id, false, col, 0.75f);
                    cell->holdsWidget = true;
                    // Hidden right now (music with nothing playing): a
                    // placeholder says what's there and when it shows
                    if (w && !w->shown()) {
                        const auto* d = find(it->id);
                        auto* ph = cell->add<Flex>(Flex::Direction::Column);
                        ph->setSpacing(4)->setAlignItems(Align::Center)->setPadding({14, 10})->setRadius(Radii(18));
                        ph->setBackground(ColorRef("on_surface", 0.08f), false);
                        ph->alignSelf = Align::Center;
                        ph->add<Icon>(d->icon, 26)->setColor("on_surface_variant", false);
                        ph->add<Label>(d->name)->setSize(14)->setWeight(600)->setColor("on_surface", false);
                        if (*whenShown(it->id)) ph->add<Label>(whenShown(it->id))->setSize(12)->setColor("on_surface_variant", false);
                    }
                } else {
                    auto* hint = cell->add<Flex>(Flex::Direction::Row);
                    hint->setSpacing(6);
                    hint->alignSelf = Align::Center;
                    hint->add<Icon>("add", 22)->setColor(selected ? "primary" : "on_surface_variant", false);
                    hint->add<Label>("Add")->setSize(14)->setWeight(selected ? 600 : 400)->setColor(selected ? "primary" : "on_surface_variant", false);
                }
                cell->onClicked = [st, select, col, row](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    // Tapped again: let go
                    if (st->col == col && st->row == row) return select(-1, -1);
                    select(col, row);
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

        // ── The side sheet ──
        auto* sheet = body->add<Flex>(Flex::Direction::Column);
        sheet->setSpacing(12)->setPadding(Insets(16))->setRadius(Radii(28))->setBackground(ColorRef("on_surface", 0.07f), false);
        sheet->setAlignItems(Align::Stretch);
        sheet->fixedWidth = 340;
        const int c = st->col, r = st->row;
        const Item* chosen = c >= 0 ? at(c, r) : nullptr;
        auto sheetHead = [&](const std::string& title, const std::string& sub, bool closable) {
            auto* h = sheet->add<Flex>(Flex::Direction::Row);
            h->setSpacing(8)->setAlignItems(Align::Start);
            auto* t = h->add<Flex>(Flex::Direction::Column);
            t->setSpacing(2)->setAlignItems(Align::Start);
            t->grow = true;
            t->add<Label>(title)->setSize(18)->setWeight(600)->setMaxWidth(250);
            if (!sub.empty()) t->add<Label>(sub)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(260)->setMaxLines(3);
            if (closable) iconButton(h, "close", [select] { select(-1, -1); });
        };

        if (chosen && !st->replacing) {
            // A placed widget: what it is, a pad to move it, Replace, Remove
            const auto* d = find(chosen->id);
            sheetHead(d->name, std::format("In the {}", cellName(c, r)), true);
            auto* about = sheet->add<Flex>(Flex::Direction::Row);
            about->setSpacing(14)->setPadding(Insets(14))->setRadius(Radii(22))->setBackground(ColorRef("on_surface", 0.07f), false);
            bubble(about, d->icon, 52, true);
            auto* txt = about->add<Flex>(Flex::Direction::Column);
            txt->setSpacing(3)->setAlignItems(Align::Start);
            txt->grow = true;
            txt->add<Label>(d->about)->setSize(13)->setMaxWidth(200)->setMaxLines(3);
            if (*whenShown(chosen->id)) txt->add<Label>(whenShown(chosen->id))->setSize(12)->setColor("primary", false);
            sheet->add<Label>("Move")->setSize(13)->setWeight(600)->setColor("primary", false);
            // A direction pad: the arrows where they point, faint at the edges
            auto* pad = sheet->add<Flex>(Flex::Direction::Column);
            pad->setSpacing(6)->setAlignItems(Align::Center);
            auto arrow = [&](Node* parent, const char* icon, int dc, int dr) {
                const int nc = c + dc, nr = r + dr;
                const bool can = nc >= 0 && nc < kCols && nr >= 0 && nr < kRows;
                auto* b = parent->add<Button>(Flex::Direction::Row);
                b->setJustify(Align::Center)->setRadius(Radii(18))->setBackground(ColorRef("secondary_container", can ? 1.0f : 0.35f), false);
                b->pressedRadius = 10;
                b->fixedWidth = b->fixedHeight = 52;
                b->add<Icon>(icon, 24)->setColor("on_secondary_container", false);
                b->setOpacity(can ? 1.0f : 0.4f, false);
                b->tooltip = can ? std::format("To the {}{}", cellName(nc, nr), at(nc, nr) ? " (swaps with what's there)" : "") : "";
                b->onClicked = [st, c, r, dc, dr, can](uint32_t button) {
                    if (button != BTN_LEFT || !can) return;
                    st->col = c + dc;
                    st->row = r + dr;
                    lockgrid::move(c, r, dc, dr); // saves; the editor follows
                };
            };
            arrow(pad, "keyboard_arrow_up", 0, -1);
            auto* mid = pad->add<Flex>(Flex::Direction::Row);
            mid->setSpacing(6)->setAlignItems(Align::Center);
            arrow(mid, "keyboard_arrow_left", -1, 0);
            auto* centre = mid->add<Flex>(Flex::Direction::Row);
            centre->setJustify(Align::Center)->setRadius(Radii(18))->setBackground(ColorRef("primary", 0.16f), false);
            centre->fixedWidth = centre->fixedHeight = 52;
            centre->add<Icon>(d->icon, 22)->setColor("primary", false);
            arrow(mid, "keyboard_arrow_right", 1, 0);
            arrow(pad, "keyboard_arrow_down", 0, 1);
            auto* actions = sheet->add<Flex>(Flex::Direction::Row);
            actions->setSpacing(8)->setPadding({0, 6, 0, 0});
            auto* replace = pillButton(actions, "Replace", false, [st, live] {
                st->replacing = true;
                live->syncSoon();
            }, "swap_horiz");
            replace->grow = true;
            auto* remove = actions->add<Button>(Flex::Direction::Row);
            pill(remove->setBackground("error_container", false))->setPadding({16, 10})->setSpacing(6)->setJustify(Align::Center);
            remove->grow = true;
            remove->add<Icon>("delete", 18)->setColor("on_error_container", false);
            remove->add<Label>("Remove")->setSize(14)->setWeight(500)->setColor("on_error_container", false);
            remove->onClicked = [c, r](uint32_t button) {
                if (button == BTN_LEFT) lockgrid::remove(c, r); // the cell stays picked: add another there
            };
            return;
        }

        // The library: every widget by kind. For a picked cell it places
        // there; otherwise Add takes the first free cell.
        const bool forCell = c >= 0;
        if (st->replacing && chosen)
            sheetHead(std::format("Replace the {}", find(chosen->id)->name), std::format("Pick what the {} shows instead", cellName(c, r)), true);
        else if (forCell)
            sheetHead(std::format("Add to the {}", cellName(c, r)), "Pick a widget for this cell", true);
        else
            sheetHead("Widgets", "Tap one to add it to the first free cell, or tap a cell first to choose where", false);
        auto* scroll = sheet->add<ScrollView>();
        scroll->blurEdges = true;
        scroll->maxHeight = 1e6f;
        scroll->grow = true;
        auto* lib = scroll->add<Flex>(Flex::Direction::Column);
        lib->setSpacing(4)->setAlignItems(Align::Stretch)->setPadding({0, 0, 6, 0});
        const auto free = firstFree(items);
        for (const char* group : groups()) {
            std::vector<const Def*> members;
            for (const auto& d : widgets())
                if (groupOf(d.id) == std::string(group)) members.push_back(&d);
            auto* label = lib->add<Label>(group);
            label->setSize(13)->setWeight(600)->setColor("primary", false);
            label->alignSelf = Align::Start;
            for (const Def* d : members) {
                const Item* placed = nullptr;
                for (const auto& o : items)
                    if (o.id == d->id) placed = &o;
                auto* row = lib->add<Button>(Flex::Direction::Row);
                row->setPadding({10, 8})->setSpacing(12)->setRadius(Radii(18))->setAlignItems(Align::Center);
                row->pressedRadius = 10;
                bubble(row, d->icon, 40, false);
                auto* txt = row->add<Flex>(Flex::Direction::Column);
                txt->setSpacing(1)->setAlignItems(Align::Start);
                txt->grow = true;
                txt->add<Label>(d->name)->setSize(14)->setWeight(600)->setMaxWidth(170);
                txt->add<Label>(placed ? std::format("In the {}", cellName(placed->col, placed->row)) : std::string(d->about))
                    ->setSize(12)
                    ->setMaxWidth(170)
                    ->setMaxLines(2)
                    ->setColor(placed ? "primary" : "on_surface_variant", false);
                const std::string id = d->id;
                if (forCell) {
                    // Placed elsewhere: it moves here
                    row->add<Icon>(placed ? "open_with" : "add_circle", 22)->setColor("primary", false);
                    row->onClicked = [id, c, r, st, live](uint32_t button) {
                        if (button != BTN_LEFT) return;
                        st->replacing = false;
                        lockgrid::put(id, c, r);
                        live->syncSoon();
                    };
                } else if (placed) {
                    // Tapping a placed one picks it on the stage
                    const int pc = placed->col, pr = placed->row;
                    row->add<Icon>("chevron_right", 22)->setColor("on_surface_variant", false);
                    row->onClicked = [select, pc, pr](uint32_t button) {
                        if (button == BTN_LEFT) select(pc, pr);
                    };
                } else if (free.first >= 0) {
                    // The whole row adds it, to the first free cell
                    row->add<Icon>("add_circle", 22)->setColor("primary", false);
                    row->onClicked = [id, free, st](uint32_t button) {
                        if (button != BTN_LEFT) return;
                        st->col = free.first;
                        st->row = free.second;
                        lockgrid::put(id, free.first, free.second);
                    };
                }
            }
        }
        if (!forCell && free.first < 0) caption(sheet, "Every cell is taken: pick a widget to replace or remove it")->setMaxWidth(300)->setMaxLines(2);
    };
    live->conns.push_back(Config::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}

} // namespace m3::shell::panels
