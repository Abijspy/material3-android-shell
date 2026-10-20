#pragma once

#include "ui/Node.h"
#include "ui/Theme.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

// What sits on the desktop: widgets (the clock, calendar, weather…), app
// shortcuts and file or folder shortcuts, each at a place given as a
// fraction of the screen (so it holds on any resolution). Kept in
// config.json as desktop.items; placed in the desktop editor.
namespace m3::shell::desktop {

struct Item {
    std::string kind; // "widget", "app" (a desktop id) or "file" (a path)
    std::string id;
    float x = 0.05f, y = 0.2f; // top-left, as fractions of the screen
    float scale = 1;           // its own size (the editor's − and +)
};

struct WidgetDef {
    const char* id;
    const char* icon;
    const char* name;
    const char* about;
};

// Every widget the desktop can hold
const std::vector<WidgetDef>& widgets();

// The saved items; without any, the desktop clock where it always was
// (unless it was turned off)
std::vector<Item> load();
void save(const std::vector<Item>& items);
std::string nameOf(const Item& item);

// The node an item shows as, live. Shortcuts open their app or file on a
// click, except while `editing` (the editor moves them instead).
ui::Node* build(ui::Node* parent, const Item& item, bool editing);

// Places its children at fractions of its own size, each kept inside it
class Board : public ui::Node {
public:
    Board() { textArea = Theme::AreaWidgets; } // Look & feel → Text size → Widgets
    std::vector<std::pair<float, float>> spots; // one per child, in order
    // After each arrangement (the desktop sets its input region from it)
    std::function<void()> onArranged;

protected:
    SizeF doMeasure(SizeF available) override { return available; }
    void doArrange() override;
};

} // namespace m3::shell::desktop
