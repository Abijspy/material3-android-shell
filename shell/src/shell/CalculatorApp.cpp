// Calculator (material3ui-calculator): sums as you type them, a scientific
// keypad, the history, and a unit converter. The arithmetic is the shell's
// own (Spotlight's): + − × ÷ ^ %, brackets, sin cos tan (degrees or
// radians), ln log √, π and e.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "services/Search.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// Typed symbols into the parser's, and degrees into radians where asked
std::string prepare(std::string e) {
    auto replaceAll = [&](const std::string& a, const std::string& b) {
        for (size_t i = 0; (i = e.find(a, i)) != std::string::npos; i += b.size()) e.replace(i, a.size(), b);
    };
    std::erase(e, ','); // thousands separators, from an answer reused
    replaceAll("×", "*");
    replaceAll("÷", "/");
    replaceAll("−", "-");
    replaceAll("π", "pi");
    replaceAll("√", "sqrt");
    replaceAll(",", ".");
    if (Config::get().value<bool>("/calculator/degrees", true)) {
        for (const char* f : {"sin(", "cos(", "tan("}) {
            for (size_t i = 0; (i = e.find(f, i)) != std::string::npos;) {
                if (i > 0 && std::isalpha(static_cast<unsigned char>(e[i - 1]))) { // asin, acos…
                    i += 4;
                    continue;
                }
                const size_t open = i + 3;
                int depth = 0;
                size_t close = std::string::npos;
                for (size_t j = open; j < e.size(); ++j) {
                    if (e[j] == '(') ++depth;
                    else if (e[j] == ')' && --depth == 0) {
                        close = j;
                        break;
                    }
                }
                if (close == std::string::npos) close = e.size(), e += ")";
                e.insert(close, ")*pi/180");
                e.insert(open + 1, "(");
                i = close + 10;
            }
        }
    }
    return e;
}

// An answer as Settings asks: as many decimals as it needs (or a fixed
// number), with thousands separated or not
std::string formatAnswer(double v) {
    const int places = Config::get().value<int>("/calculator/decimals", -1);
    std::string s = places < 0 ? m3::formatNumber(v) : std::format("{:.{}f}", v, std::clamp(places, 0, 12));
    if (Config::get().value<bool>("/calculator/grouping", false) && s.find('e') == std::string::npos) {
        const size_t start = s.starts_with('-') ? 1 : 0;
        size_t end = s.find('.');
        if (end == std::string::npos) end = s.size();
        for (long i = static_cast<long>(end) - 3; i > static_cast<long>(start); i -= 3) s.insert(static_cast<size_t>(i), ",");
    }
    return s;
}

std::string result(const std::string& expr) {
    if (expr.empty()) return "";
    auto v = m3::evaluate(prepare(expr));
    if (!v) return "";
    return formatAnswer(*v);
}

// ── Units ──

struct Unit {
    const char* name;
    double factor; // to the category's base
};
struct Category {
    const char *name, *icon;
    std::vector<Unit> units;
};
const std::vector<Category>& categories() {
    static const std::vector<Category> c = {
        {"Length", "straighten", {{"mm", .001}, {"cm", .01}, {"m", 1}, {"km", 1000}, {"in", .0254}, {"ft", .3048}, {"yd", .9144}, {"mi", 1609.344}, {"nmi", 1852}}},
        {"Mass", "scale", {{"mg", 1e-6}, {"g", .001}, {"kg", 1}, {"t", 1000}, {"oz", .028349523125}, {"lb", .45359237}, {"st", 6.35029318}}},
        {"Temperature", "thermostat", {{"°C", 0}, {"°F", 0}, {"K", 0}}},
        {"Area", "square_foot", {{"cm²", 1e-4}, {"m²", 1}, {"ha", 1e4}, {"km²", 1e6}, {"in²", 6.4516e-4}, {"ft²", .09290304}, {"acre", 4046.8564224}, {"mi²", 2589988.110336}}},
        {"Volume", "water_drop", {{"ml", .001}, {"l", 1}, {"m³", 1000}, {"tsp", .00492892}, {"tbsp", .0147868}, {"fl oz", .0295735295625}, {"cup", .2365882365},
                                  {"pint", .473176473}, {"gal", 3.785411784}}},
        {"Speed", "speed", {{"m/s", 1}, {"km/h", 1 / 3.6}, {"mph", .44704}, {"knot", .514444}, {"ft/s", .3048}}},
        {"Data", "storage", {{"bit", .125}, {"B", 1}, {"KB", 1e3}, {"MB", 1e6}, {"GB", 1e9}, {"TB", 1e12}, {"KiB", 1024}, {"MiB", 1048576}, {"GiB", 1073741824}, {"TiB", 1099511627776}}},
        {"Time", "schedule", {{"ms", .001}, {"s", 1}, {"min", 60}, {"h", 3600}, {"day", 86400}, {"week", 604800}, {"year", 31557600}}},
    };
    return c;
}
double convert(const Category& c, int from, int to, double v) {
    if (std::string(c.name) == "Temperature") {
        double k = from == 0 ? v + 273.15 : from == 1 ? (v - 32) * 5 / 9 + 273.15 : v;
        return to == 0 ? k - 273.15 : to == 1 ? (k - 273.15) * 9 / 5 + 32 : k;
    }
    return v * c.units[static_cast<size_t>(from)].factor / c.units[static_cast<size_t>(to)].factor;
}

struct State {
    int tab = 0;
    bool scientific = false;
    TextField* expr = nullptr;
    Label* answer = nullptr;
    Flex *body = nullptr, *history = nullptr;
    int cat = 0, from = 2, to = 3;
    std::string amount = "1";
    std::string text; // the sum typed, kept across the tabs
    std::function<void()> show, showHistory;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

void remember(const std::string& e, const std::string& r) {
    auto h = Config::get().value<nlohmann::json>("/calculator/history", nlohmann::json::array());
    h.insert(h.begin(), nlohmann::json{{"e", e}, {"r", r}});
    const size_t keep = static_cast<size_t>(std::clamp(Config::get().value<int>("/calculator/historyLength", 30), 5, 200));
    while (h.size() > keep) h.erase(h.end() - 1);
    Config::get().set("/calculator/history", h);
}

void insert(const std::shared_ptr<State>& st, const std::string& t) {
    st->expr->setText(st->expr->text() + t);
    st->answer->setText(result(st->expr->text()));
    st->expr->focus();
}

void equals(const std::shared_ptr<State>& st) {
    const std::string e = st->expr->text();
    const std::string r = result(e);
    if (r.empty()) {
        st->answer->setText(e.empty() ? "" : "Not a sum it can work out");
        return;
    }
    remember(e, r);
    st->expr->setText(r);
    st->answer->setText("");
    if (st->showHistory) st->showHistory();
}

void keypad(const std::shared_ptr<State>& st, Flex* c) {
    auto grid = [&](const std::vector<std::vector<std::string>>& rows, bool sci) {
        for (const auto& row : rows) {
            auto* r = c->add<Flex>(Flex::Direction::Row);
            r->setSpacing(8);
            for (const auto& k : row) {
                auto* b = r->add<Button>();
                b->grow = true;
                b->fixedWidth = 1;
                b->fixedHeight = sci ? 48 : 62;
                b->setRadius(Radii(sci ? 18 : 24))->setJustify(Align::Center)->setAlignItems(Align::Center);
                const bool op = k == "÷" || k == "×" || k == "−" || k == "+";
                const bool eq = k == "=";
                const bool fn = k == "C" || k == "⌫" || k == "( )" || k == "%";
                b->setBackground(eq ? "primary" : op ? "secondary_container" : fn ? "tertiary_container" : sci ? "surface_container_highest" : "surface_container_high", false);
                b->add<Label>(k)->setSize(sci ? 16 : 22)->setWeight(500)->setColor(eq ? "on_primary" : op ? "on_secondary_container" : fn ? "on_tertiary_container" : "on_surface", false);
                b->onClicked = [st, k](uint32_t btn) {
                    if (btn != BTN_LEFT) return;
                    if (k == "=") return equals(st);
                    if (k == "C") {
                        st->expr->setText("");
                        st->answer->setText("");
                        return;
                    }
                    if (k == "⌫") {
                        std::string t = st->expr->text();
                        if (!t.empty()) {
                            // A whole character (π, ×…), not half of it
                            size_t cut = t.size() - 1;
                            while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;
                            t.erase(cut);
                        }
                        st->expr->setText(t);
                        st->answer->setText(result(t));
                        return;
                    }
                    if (k == "( )") {
                        const std::string t = st->expr->text();
                        const long open = std::ranges::count(t, '('), close = std::ranges::count(t, ')');
                        return insert(st, open > close && !t.empty() && (std::isdigit(static_cast<unsigned char>(t.back())) || t.back() == ')') ? ")" : "(");
                    }
                    if (k == "±") return insert(st, "−");
                    if (k == "x²") return insert(st, "^2");
                    if (k == "xʸ") return insert(st, "^");
                    if (k == "1/x") return insert(st, "1/");
                    if (k == "sin" || k == "cos" || k == "tan" || k == "ln" || k == "log" || k == "√") return insert(st, k + "(");
                    insert(st, k);
                };
            }
        }
    };
    if (st->scientific) grid({{"sin", "cos", "tan", "π"}, {"ln", "log", "√", "e"}, {"x²", "xʸ", "1/x", "%"}}, true);
    grid({{"C", "( )", "⌫", "÷"}, {"7", "8", "9", "×"}, {"4", "5", "6", "−"}, {"1", "2", "3", "+"}, {"±", "0", ".", "="}}, false);
}

void converter(const std::shared_ptr<State>& st, Flex* c) {
    auto* chips = c->add<Flow>();
    chips->spacing = 6;
    for (int i = 0; i < static_cast<int>(categories().size()); ++i) {
        const auto& cat = categories()[static_cast<size_t>(i)];
        auto* b = chips->add<Button>(Flex::Direction::Row);
        pill(b)->setPadding({12, 6})->setSpacing(6);
        b->setBackground(st->cat == i ? "secondary_container" : "surface_container_high", false);
        b->add<Icon>(cat.icon, 16)->setColor(st->cat == i ? "on_secondary_container" : "on_surface_variant", false);
        b->add<Label>(cat.name)->setSize(13)->setWeight(500);
        b->onClicked = [st, i](uint32_t btn) {
            if (btn != BTN_LEFT) return;
            st->cat = i;
            st->from = 0;
            st->to = std::min<int>(1, static_cast<int>(categories()[static_cast<size_t>(i)].units.size()) - 1);
            EventLoop::get().defer([st] {
                if (*st->alive && st->show) st->show();
            });
        };
    }
    const auto& cat = categories()[static_cast<size_t>(st->cat)];
    std::vector<std::pair<std::string, std::string>> units;
    for (int i = 0; i < static_cast<int>(cat.units.size()); ++i) units.push_back({std::to_string(i), cat.units[static_cast<size_t>(i)].name});
    auto side = [&](bool from) {
        auto* box = c->add<Flex>(Flex::Direction::Row);
        box->setPadding({18, 14})->setSpacing(12)->setAlignItems(Align::Center)->setRadius(Radii(24))->setBackground("surface_container_high", false);
        if (from) {
            auto* f = box->add<TextField>();
            f->bare = true;
            f->grow = true;
            f->setText(st->amount);
            f->onChanged = [st](const std::string& t) {
                st->amount = t;
                EventLoop::get().defer([st] {
                    if (!*st->alive || !st->show) return;
                    st->show();
                    // Typing goes on in the new field
                    for (auto& ch : st->body->children())
                        for (auto& g : ch->children())
                            if (auto* tf = dynamic_cast<TextField*>(g.get())) {
                                tf->focus();
                                return;
                            }
                });
            };
        } else {
            const auto v = m3::evaluate(prepare(st->amount));
            auto* l = box->add<Label>(v ? m3::formatNumber(convert(cat, st->from, st->to, *v)) : "—");
            l->setSize(28)->setWeight(500);
            l->grow = true;
        }
        dropdown(box, box, cat.units[static_cast<size_t>(from ? st->from : st->to)].name, units, [st, from](const std::string& v) {
            (from ? st->from : st->to) = std::stoi(v);
            EventLoop::get().defer([st] {
                if (*st->alive && st->show) st->show();
            });
        });
    };
    side(true);
    auto* swap = c->add<Flex>(Flex::Direction::Row);
    swap->setJustify(Align::Center);
    iconButton(swap, "swap_vert", [st] {
        std::swap(st->from, st->to);
        EventLoop::get().defer([st] {
            if (*st->alive && st->show) st->show();
        });
    }, 24)->tooltip = "Swap";
    side(false);
}

class CalcKeeper : public Node {
public:
    explicit CalcKeeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~CalcKeeper() override {
        *m_st->alive = false;
        m_st->show = m_st->showHistory = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

void calculatorApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<State>();
    st->scientific = Config::get().value<bool>("/calculator/scientific", false);
    card->setPadding({16, 6, 16, 16})->setSpacing(12);
    auto* top = card->add<Flex>(Flex::Direction::Row);
    top->setSpacing(10)->setAlignItems(Align::Center);
    auto* tabsBox = top->add<Flex>(Flex::Direction::Row);
    tabsBox->grow = true;
    auto* cols = card->add<Flex>(Flex::Direction::Row);
    cols->setSpacing(16)->setAlignItems(Align::Stretch);
    cols->grow = true;
    st->body = cols->add<Flex>(Flex::Direction::Column);
    st->body->setSpacing(8)->setAlignItems(Align::Stretch);
    st->body->grow = true;
    auto* hscroll = cols->add<ScrollView>();
    hscroll->maxHeight = 1e6f;
    hscroll->fixedWidth = 220;
    st->history = hscroll->add<Flex>(Flex::Direction::Column);
    st->history->setSpacing(4)->setAlignItems(Align::Stretch);
    card->add<CalcKeeper>(st);

    connectedGroup<int>(tabsBox, 0, {{0, "Calculator"}, {1, "Convert"}}, [st](int v) {
        st->tab = v;
        EventLoop::get().defer([st] {
            if (*st->alive && st->show) st->show();
        });
    });
    auto* sci = iconButton(top, "function", [st] {
        st->scientific = !st->scientific;
        Config::get().set("/calculator/scientific", st->scientific);
        EventLoop::get().defer([st] {
            if (*st->alive && st->show) st->show();
        });
    }, 22);
    sci->tooltip = "Scientific keys";

    st->show = [st] {
        auto* c = st->body;
        if (st->expr) st->text = st->expr->text();
        const std::string keep = st->text;
        c->clearChildren();
        st->expr = nullptr;
        if (st->tab == 1) return converter(st, c);
        auto* disp = c->add<Flex>(Flex::Direction::Column);
        disp->setPadding({20, 14})->setSpacing(4)->setRadius(Radii(28))->setBackground("surface_container_high", false)->setAlignItems(Align::Stretch);
        st->expr = disp->add<TextField>();
        st->expr->bare = true;
        st->expr->setPlaceholder("0");
        st->expr->setText(keep);
        st->expr->onChanged = [st](const std::string& t) { st->answer->setText(result(t)); };
        st->expr->onSubmit = [st](const std::string&) { equals(st); };
        st->answer = disp->add<Label>(result(keep));
        st->answer->setSize(30)->setWeight(500)->setColor("primary", false);
        st->answer->fixedHeight = 40; // the keys don't move as answers come and go
        st->answer->alignSelf = Align::End;
        caption(disp, Config::get().value<bool>("/calculator/degrees", true) ? "Degrees" : "Radians");
        keypad(st, c);
        EventLoop::get().defer([st] {
            if (*st->alive && st->expr) st->expr->focus();
        });
    };
    st->showHistory = [st] {
        auto* h = st->history;
        h->clearChildren();
        const auto list = Config::get().value<nlohmann::json>("/calculator/history", nlohmann::json::array());
        auto* head = h->add<Flex>(Flex::Direction::Row);
        head->setAlignItems(Align::Center);
        auto* t = head->add<Label>("History");
        t->setSize(14)->setWeight(600)->setColor("on_surface_variant", false);
        t->grow = true;
        if (!list.empty()) iconButton(head, "delete_sweep", [st] {
            Config::get().set("/calculator/history", nlohmann::json::array());
            if (st->showHistory) st->showHistory();
        }, 18)->tooltip = "Clear";
        if (list.empty()) caption(h, "Your sums show here");
        for (const auto& item : list) {
            const std::string e = item.value("e", ""), r = item.value("r", "");
            auto* b = h->add<Button>(Flex::Direction::Column);
            b->setPadding({12, 8})->setRadius(Radii(14))->setAlignItems(Align::End);
            b->add<Label>(e)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(190);
            b->add<Label>("= " + r)->setSize(16)->setWeight(500)->setMaxWidth(190);
            b->onClicked = [st, r](uint32_t btn) {
                if (btn == BTN_LEFT && st->expr) insert(st, r);
            };
        }
    };
    popup.onKey = [st](const KeyEvent& e) {
        if (st->tab != 0 || !st->expr) return false;
        if (e.sym == XKB_KEY_Escape) {
            st->expr->setText("");
            st->answer->setText("");
            return true;
        }
        return false;
    };
    st->show();
    st->showHistory();
    if (!arg.empty() && st->expr) {
        st->expr->setText(arg);
        st->answer->setText(result(arg));
    }
}

void calculatorSettings(Flex* page) {
    auto* g = group(page, "Calculating");
    choiceRow<bool>(g, "architecture", "Angles", "For sin, cos and tan", "/calculator/degrees", true, {{true, "Degrees"}, {false, "Radians"}});
    switchRow(g, "function", "Scientific keys", "sin, cos, tan, ln, log, √, π, e and powers", "/calculator/scientific", false);
    auto r = row(g, "history", "History", "Your last 30 sums");
    pillButton(r.row, "Clear", false, [] { Config::get().set("/calculator/history", nlohmann::json::array()); }, "delete_sweep");
    auto f = group(page, "Answers");
    choiceRow<int>(f, "decimal_increase", "Decimal places", "", "/calculator/decimals", -1, {{-1, "As many as needed"}, {2, "2"}, {4, "4"}, {6, "6"}, {10, "10"}});
    switchRow(f, "pin", "Separate thousands", "1,234,567 instead of 1234567", "/calculator/grouping", false);
    choiceRow<int>(f, "history", "Sums remembered", "", "/calculator/historyLength", 30, {{10, "10"}, {30, "30"}, {100, "100"}});
}

} // namespace m3::shell::panels
