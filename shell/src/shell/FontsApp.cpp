// Fonts: the fonts installed, a family at a time. The list down the side
// shows each family in its own face (search, monospace, yours); the page
// shows its styles, a sample in sizes from small to display, and the
// characters, with the text yours to change. A font file opened from Files
// shows the same way, with Install (into ~/.local/share/fonts); fonts you
// installed can be removed. It's the `material3ui-fonts` app.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

struct Face {
    std::string style, file;
    int weight = 400; // CSS: 100 thin … 900 black
    bool italic = false, oblique = false;
};

struct Family {
    std::string name;
    std::vector<Face> faces;
    bool mono = false, mine = false, opened = false; // opened: a file shown, not installed
};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string userFontDir() {
    const char* x = std::getenv("XDG_DATA_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.local/share") + "/fonts";
}

bool isMine(const std::string& file) {
    return file.starts_with(userFontDir() + "/") || file.starts_with(paths::home() + "/.fonts/");
}

// Every font fontconfig knows (the system's, yours, and files opened here),
// by family, A to Z; each family's styles from regular outwards
std::vector<Family> listFonts() {
    FcPattern* pat = FcPatternCreate();
    FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_FILE, FC_WEIGHT, FC_SLANT, FC_SPACING, nullptr);
    FcFontSet* set = FcFontList(nullptr, pat, os);
    std::map<std::string, Family> byName;
    for (int i = 0; set && i < set->nfont; ++i) {
        FcPattern* f = set->fonts[i];
        FcChar8 *family = nullptr, *style = nullptr, *file = nullptr;
        if (FcPatternGetString(f, FC_FAMILY, 0, &family) != FcResultMatch || !family) continue;
        FcPatternGetString(f, FC_STYLE, 0, &style);
        FcPatternGetString(f, FC_FILE, 0, &file);
        int weight = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN, spacing = FC_PROPORTIONAL;
        FcPatternGetInteger(f, FC_WEIGHT, 0, &weight);
        FcPatternGetInteger(f, FC_SLANT, 0, &slant);
        FcPatternGetInteger(f, FC_SPACING, 0, &spacing);
        const std::string name = reinterpret_cast<const char*>(family);
        if (name.starts_with(".")) continue; // hidden system faces
        auto& fam = byName[lower(name)];
        if (fam.name.empty()) fam.name = name;
        Face face;
        face.style = style ? reinterpret_cast<const char*>(style) : "Regular";
        face.file = file ? reinterpret_cast<const char*>(file) : "";
        face.weight = std::clamp(FcWeightToOpenType(weight), 100, 1000);
        face.italic = slant == FC_SLANT_ITALIC;
        face.oblique = slant == FC_SLANT_OBLIQUE;
        if (std::ranges::any_of(fam.faces, [&](const Face& x) { return x.style == face.style; })) continue; // the same style twice (formats)
        fam.faces.push_back(face);
        if (spacing == FC_MONO || spacing == FC_DUAL || spacing == FC_CHARCELL) fam.mono = true;
        if (isMine(face.file)) fam.mine = true;
    }
    if (set) FcFontSetDestroy(set);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    std::vector<Family> out;
    for (auto& [k, f] : byName) {
        std::ranges::sort(f.faces, [](const Face& a, const Face& b) {
            if (a.italic + a.oblique != b.italic + b.oblique) return (a.italic + a.oblique) < (b.italic + b.oblique);
            return std::abs(a.weight - 400) < std::abs(b.weight - 400) || (std::abs(a.weight - 400) == std::abs(b.weight - 400) && a.weight < b.weight);
        });
        out.push_back(std::move(f));
    }
    return out;
}

// How Pango is asked for a face: the family (a trailing comma keeps a name
// ending in a number from reading as a size), its slant; the weight's set apart
std::string describe(const std::string& family, const Face* face) {
    std::string d = family + ",";
    if (face && face->italic) d += " Italic";
    else if (face && face->oblique) d += " Oblique";
    return d;
}

// A font file shown before it's installed: fontconfig and Pango told of it
std::string openFile(const std::string& file) {
    if (!FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8*>(file.c_str()))) return "";
    if (auto* map = pango_cairo_font_map_get_default(); PANGO_IS_FC_FONT_MAP(map)) pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
    // Its family, from the app's own fonts (or the system's, when it's installed already)
    for (auto which : {FcSetApplication, FcSetSystem}) {
        FcFontSet* set = FcConfigGetFonts(nullptr, which);
        for (int i = 0; set && i < set->nfont; ++i) {
            FcChar8 *f = nullptr, *fam = nullptr;
            if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &f) != FcResultMatch || !f || file != reinterpret_cast<const char*>(f)) continue;
            if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &fam) == FcResultMatch && fam) return reinterpret_cast<const char*>(fam);
        }
    }
    return "";
}

bool isFontFile(const std::string& path) {
    const std::string ext = lower(fs::path(path).extension().string());
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".otc" || ext == ".woff" || ext == ".woff2" || ext == ".pfb";
}

const char* kSampleDefault = "The quick brown fox jumps over the lazy dog";

struct FontState {
    std::vector<Family> all;
    std::string query, filter = "all", selected, style, sample;
    std::vector<std::string> openedFiles; // shown, not installed
    Flex *list = nullptr, *page = nullptr;
    Label* count = nullptr;
    std::function<void()> showList, showPage;
    EventLoop::TimerId searchTimer = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    Family* find(const std::string& name) {
        for (auto& f : all)
            if (f.name == name) return &f;
        return nullptr;
    }
};

class FontKeeper : public Node {
public:
    explicit FontKeeper(std::shared_ptr<FontState> st) : m_st(std::move(st)) { floating = true; }
    ~FontKeeper() override {
        *m_st->alive = false;
        if (m_st->searchTimer) EventLoop::get().cancel(m_st->searchTimer);
        m_st->showList = m_st->showPage = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<FontState> m_st;
};

void reload(const std::shared_ptr<FontState>& st) {
    st->all = listFonts();
    for (const auto& f : st->openedFiles)
        for (auto& fam : st->all)
            for (const auto& face : fam.faces)
                if (face.file == f) fam.opened = true;
    if (st->showList) st->showList();
    if (st->showPage) st->showPage();
}

// Into ~/.local/share/fonts, for every app; then fontconfig's cache again
void install(const std::shared_ptr<FontState>& st, const Family& fam) {
    std::error_code ec;
    fs::create_directories(userFontDir(), ec);
    std::vector<std::string> done;
    for (const auto& face : fam.faces) {
        if (std::ranges::find(st->openedFiles, face.file) == st->openedFiles.end()) continue;
        const std::string to = userFontDir() + "/" + fileops::freeName(userFontDir(), fs::path(face.file).filename().string());
        fs::copy_file(face.file, to, ec);
        if (!ec) done.push_back(face.file);
    }
    if (done.empty()) return;
    for (const auto& f : done) std::erase(st->openedFiles, f);
    m3::run({"fc-cache", "-f", userFontDir()}, [st, name = fam.name](int, const std::string&) {
        if (!*st->alive) return;
        FcInitReinitialize();
        if (auto* map = pango_cairo_font_map_get_default(); PANGO_IS_FC_FONT_MAP(map)) pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
        reload(st);
        m3::run({"notify-send", "-a", "Fonts", "-i", "preferences-desktop-font", "Font installed", name + " is ready in every app"}, nullptr);
    });
}

// Yours only (the system's belong to packages): to the trash
void removeFamily(const std::shared_ptr<FontState>& st, const Family& fam) {
    std::vector<std::string> files;
    for (const auto& face : fam.faces)
        if (isMine(face.file)) files.push_back(face.file);
    if (files.empty()) return;
    fileops::trash(files);
    m3::run({"fc-cache", "-f", userFontDir()}, [st](int, const std::string&) {
        if (!*st->alive) return;
        FcInitReinitialize();
        if (auto* map = pango_cairo_font_map_get_default(); PANGO_IS_FC_FONT_MAP(map)) pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
        st->selected.clear();
        reload(st);
    });
}

void buildList(const std::shared_ptr<FontState>& st) {
    auto* l = st->list;
    l->clearChildren();
    const std::string q = lower(st->query);
    const bool ownFace = Config::get().value<bool>("/fonts/ownFace", true);
    int shown = 0, matching = 0;
    for (const auto& f : st->all) {
        if (!q.empty() && lower(f.name).find(q) == std::string::npos) continue;
        if (st->filter == "mono" && !f.mono) continue;
        if (st->filter == "mine" && !f.mine && !f.opened) continue;
        ++matching;
        if (++shown > 600) continue;
        const bool here = f.name == st->selected;
        auto* b = l->add<Button>(Flex::Direction::Column);
        pill(b)->setPadding({16, 9})->setSpacing(1)->setAlignItems(Align::Start);
        b->pressedRadius = 12;
        if (here) b->setBackground("secondary_container", false);
        auto* name = b->add<Label>(f.name);
        name->setSize(17)->setMaxWidth(240)->setColor(here ? "on_secondary_container" : "on_surface", false);
        if (ownFace) name->setFamily(describe(f.name, nullptr));
        b->add<Label>(f.opened ? "Not installed" : std::format("{} style{}{}", f.faces.size(), f.faces.size() == 1 ? "" : "s", f.mono ? "  ·  Monospace" : ""))
            ->setSize(11)
            ->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
        b->onClicked = [st, n = f.name](uint32_t button) {
            if (button != BTN_LEFT) return;
            st->selected = n;
            st->style.clear();
            st->showList();
            st->showPage();
        };
    }
    if (shown > 600) caption(l, std::format("The first 600 of {}: search to find the others", matching));
    if (matching == 0) caption(l, st->query.empty() ? "No fonts here" : "No font is called that");
    st->count->setText(std::format("{} famil{}", matching, matching == 1 ? "y" : "ies"));
}

void buildPage(const std::shared_ptr<FontState>& st) {
    auto* p = st->page;
    p->clearChildren();
    Family* fam = st->find(st->selected);
    if (!fam) {
        auto* none = p->add<Flex>(Flex::Direction::Column);
        none->setAlignItems(Align::Center)->setSpacing(10)->setPadding({0, 120});
        none->add<Icon>("font_download", 56)->setColor("on_surface_variant", false);
        none->add<Label>("Pick a font")->setSize(16)->setColor("on_surface_variant", false);
        return;
    }
    const Face* face = &fam->faces.front();
    for (const auto& f : fam->faces)
        if (f.style == st->style) face = &f;
    const std::string desc = describe(fam->name, face);

    // The family, in itself
    auto* head = p->add<Flex>(Flex::Direction::Row);
    head->setSpacing(12)->setAlignItems(Align::Center);
    auto* titles = head->add<Flex>(Flex::Direction::Column);
    titles->setAlignItems(Align::Start)->setSpacing(2);
    titles->grow = true;
    titles->givesWay = true;
    titles->add<Label>(fam->name)->setSize(34)->setFamily(desc)->setWeight(face->weight)->setMaxWidth(560);
    std::string ext = lower(fs::path(face->file).extension().string());
    if (!ext.empty()) ext = ext.substr(1);
    for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    caption(titles, std::format("{} style{}{}{}{}", fam->faces.size(), fam->faces.size() == 1 ? "" : "s", fam->mono ? "  ·  Monospace" : "",
                                ext.empty() ? "" : "  ·  " + ext, fam->opened ? "  ·  Not installed" : fam->mine ? "  ·  Installed by you" : ""));
    if (fam->opened)
        pillButton(head, "Install", true, [st, f = *fam] { install(st, f); }, "download");
    iconButton(head, "content_copy", [n = fam->name] { m3::run({"wl-copy"}, nullptr, n); }, 20)->tooltip = "Copy the name";
    iconButton(head, "folder_open", [file = face->file] {
        if (open) open("files " + file);
    }, 20)->tooltip = "Show in Files";
    if (fam->mine && !fam->opened) {
        Button* rm = iconButton(head, "delete", [] {}, 20);
        rm->tooltip = "Remove (to the trash)";
        rm->onClicked = [st, f = *fam, rm](uint32_t b) {
            if (b == BTN_LEFT)
                floatingMenu(rm, {MenuItem("delete", "Remove " + f.name, "Its files go to the trash", [st, f] { removeFamily(st, f); }, false, true)});
        };
    }

    // Its styles
    if (fam->faces.size() > 1) {
        auto* styles = p->add<Flow>();
        styles->spacing = 6;
        for (const auto& f : fam->faces) {
            const bool on = &f == face;
            auto* c = styles->add<Button>(Flex::Direction::Row);
            pill(c->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface_container_high"), false))->setPadding({12, 6});
            c->add<Label>(f.style)->setSize(13)->setFamily(describe(fam->name, &f))->setWeight(f.weight)->setColor(on ? "on_secondary_container" : "on_surface", false);
            c->onClicked = [st, s = f.style](uint32_t b) {
                if (b != BTN_LEFT) return;
                st->style = s;
                st->showPage();
            };
        }
    }

    // The sample: yours to type
    auto* field = p->add<Flex>(Flex::Direction::Row);
    pill(field->setBackground("surface_container_high", false))->setPadding({14, 4})->setSpacing(8)->setAlignItems(Align::Center);
    field->add<Icon>("edit", 18)->setColor("on_surface_variant", false);
    auto* tf = field->add<TextField>();
    tf->bare = true;
    tf->grow = true;
    tf->setPlaceholder("Type something to see it in this font");
    tf->setText(st->sample);
    auto* sizes = p->add<Flex>(Flex::Direction::Column);
    sizes->setSpacing(10)->setAlignItems(Align::Start);
    auto fillSizes = [st, sizes, desc, weight = face->weight] {
        sizes->clearChildren();
        const std::string text = st->sample.empty() ? Config::get().value<std::string>("/fonts/sample", kSampleDefault) : st->sample;
        const std::string range = Config::get().value<std::string>("/fonts/previewSizes", "all");
        const std::vector<float> all = {12.0f, 16.0f, 20.0f, 28.0f, 36.0f, 48.0f, 64.0f, 80.0f};
        const std::vector<float> pxs = range == "text" ? std::vector<float>(all.begin(), all.begin() + 4) : range == "display" ? std::vector<float>(all.begin() + 3, all.end()) : all;
        for (float px : pxs) {
            auto* row = sizes->add<Flex>(Flex::Direction::Row);
            row->setSpacing(14)->setAlignItems(Align::Center);
            auto* n = row->add<Label>(std::format("{:.0f}", px));
            n->setSize(11)->setColor("on_surface_variant", false);
            n->fixedWidth = 22;
            row->add<Label>(text)->setSize(px)->setFamily(desc)->setWeight(weight)->setMaxWidth(560);
        }
    };
    fillSizes();
    tf->onChanged = [st, fillSizes](const std::string& t) {
        st->sample = t;
        fillSizes();
    };

    // The characters
    auto* chars = p->add<Flex>(Flex::Direction::Column);
    chars->setPadding(Insets(16))->setSpacing(6)->setRadius(Radii(20))->setBackground("surface_container", false)->setAlignItems(Align::Start);
    for (const char* line : {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz", "0123456789  ÀÉÎÕÜ àéîõü ß Ææ Œœ Ñ ñ",
                             "! ? & @ # $ % ^ * ( ) [ ] { } < > / \\ | + − = ~ ' \" , . : ; « » “ ” — …"})
        chars->add<Label>(line)->setSize(24)->setFamily(desc)->setWeight(face->weight)->setMaxWidth(540)->setMaxLines(2);

    // Where it lives
    auto* info = p->add<Flex>(Flex::Direction::Column);
    info->setSpacing(2)->setAlignItems(Align::Start);
    caption(info, fileops::displayPath(face->file));
}

} // namespace

std::function<void(const std::string&)> fontsGo;

void fontsApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<FontState>();
    st->filter = Config::get().value<std::string>("/fonts/filter", "all");
    card->setPadding(Insets(10))->setSpacing(8);
    card->add<FontKeeper>(st);

    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(12)->setAlignItems(Align::Stretch);
    columns->grow = true;

    // The side: search, a filter, the families
    auto* side = columns->add<Flex>(Flex::Direction::Column);
    side->setSpacing(8)->setAlignItems(Align::Stretch);
    side->fixedWidth = 290;
    auto* searchBox = side->add<Flex>(Flex::Direction::Row);
    pill(searchBox->setBackground("surface_container_high", false))->setPadding({14, 4})->setSpacing(8)->setAlignItems(Align::Center);
    searchBox->add<Icon>("search", 18)->setColor("on_surface_variant", false);
    auto* search = searchBox->add<TextField>();
    search->bare = true;
    search->grow = true;
    search->setPlaceholder("Search fonts");
    search->onChanged = [st](const std::string& t) {
        st->query = t;
        EventLoop::get().debounce(st->searchTimer, 150, [st] {
            if (*st->alive && st->showList) st->showList();
        });
    };
    auto* filters = side->add<Flex>(Flex::Direction::Row);
    filters->setSpacing(6)->setAlignItems(Align::Center);
    std::vector<Button*> chips;
    auto paintChips = std::make_shared<std::function<void()>>();
    for (const auto& [key, label] : {std::pair{"all", "All"}, {"mono", "Monospace"}, {"mine", "Yours"}}) {
        auto* c = filters->add<Button>();
        pill(c)->setPadding({12, 6});
        c->add<Label>(label)->setSize(13)->setWeight(500);
        c->onClicked = [st, k = std::string(key), paintChips](uint32_t b) {
            if (b != BTN_LEFT) return;
            st->filter = k;
            Config::get().set("/fonts/filter", k);
            (*paintChips)();
            st->showList();
        };
        chips.push_back(c);
    }
    *paintChips = [st, chips] {
        const char* keys[] = {"all", "mono", "mine"};
        for (size_t i = 0; i < chips.size(); ++i) chips[i]->setBackground(st->filter == keys[i] ? "secondary_container" : "surface_container_high", false);
    };
    (*paintChips)();
    st->count = filters->add<Label>("");
    st->count->setSize(12)->setColor("on_surface_variant", false);
    st->count->grow = true;
    auto* listScroll = side->add<ScrollView>();
    listScroll->maxHeight = 1e6f;
    listScroll->grow = true;
    listScroll->blurEdges = true;
    st->list = listScroll->add<Flex>(Flex::Direction::Column);
    st->list->setSpacing(2)->setAlignItems(Align::Stretch)->setPadding({0, 4});

    // The page
    auto* pageScroll = columns->add<ScrollView>();
    pageScroll->maxHeight = 1e6f;
    pageScroll->grow = true;
    pageScroll->blurEdges = true;
    st->page = pageScroll->add<Flex>(Flex::Direction::Column);
    st->page->setSpacing(16)->setAlignItems(Align::Stretch)->setPadding({12, 8, 16, 24});

    st->showList = [st] { buildList(st); };
    st->showPage = [st] { buildPage(st); };

    // A font file (from Files, or dropped on the window): shown, to install
    auto openPath = [st](const std::string& path) {
        std::error_code ec;
        if (path.empty() || !fs::is_regular_file(path, ec) || !isFontFile(path)) return;
        const std::string family = openFile(path);
        if (family.empty()) {
            m3::run({"notify-send", "-a", "Fonts", "This font can't be opened", fs::path(path).filename().string()}, nullptr);
            return;
        }
        if (!isMine(path) && !path.starts_with("/usr/share/fonts/")) st->openedFiles.push_back(path);
        st->selected = family;
        st->style.clear();
        reload(st);
    };
    card->onDropFiles = [openPath](const std::vector<std::string>& paths) {
        for (const auto& p : paths) openPath(p);
    };
    fontsGo = [st, openPath](const std::string& path) {
        if (*st->alive) openPath(path);
    };

    popup.onKey = [search](const KeyEvent& e) {
        if (e.ctrl() && (e.sym == XKB_KEY_f || e.sym == XKB_KEY_F)) return search->focus(), true;
        return false;
    };

    st->all = listFonts();
    if (!st->all.empty()) {
        // A familiar one to start: the theme's own font, else the first
        const std::string theme = Theme::get().fontFamily();
        st->selected = st->find(theme) ? theme : st->all.front().name;
    }
    buildList(st);
    buildPage(st);
    if (!arg.empty()) openPath(arg);
}

void fontsClosed() {
    fontsGo = nullptr;
}

// Settings → Fonts (the app)
void fontsSettings(Flex* page) {
    auto* g = group(page, "Fonts");
    switchRow(g, "format_size", "Families in their own fonts", "The list shows each name in its face; off, quicker with many fonts", "/fonts/ownFace", true);
    textRow(g, "edit", "Sample text", "/fonts/sample", kSampleDefault);
    choiceRow<std::string>(g, "format_size", "Preview sizes", "On a font's page", "/fonts/previewSizes", "all", {{"all", "12 to 80"}, {"text", "Text sizes (12 to 28)"}, {"display", "Headline sizes (28 to 80)"}});
    caption(page, "  Fonts you install go to ~/.local/share/fonts, for every app");
}

} // namespace m3::shell::panels
