// The KDL reader Niri Settings starts from: niri's syntax (comments of all
// three kinds, escapes, raw strings, numbers), what a syntax error leaves,
// and queries that merge sections as niri does (the last value wins)
#include "services/Kdl.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main(int argc, char** argv) {
    std::string error;
    auto doc = kdl::parse(R"kdl(
// a comment
layout {
    gaps 16
    center-focused-column "never"
    border { off; width 4; }
    struts {
        /- left 64
        top 8 /* inline */ bottom 0x10
    }
}
/* a block /* nested */ comment */
/-layout { gaps 99; }
window-rule {
    match app-id=r#"^org\.wezfurlong\.wezterm$"# title="a \"b\"\u{e9}"
    opacity 0.95
    open-floating true
}
prefer-no-csd
animations { slowdown 1_000.5e-3; off }
layout { gaps -2; border { on; } }
spawn-at-startup "waybar" \
    "--flag"
(type)annotated #true #null null
)kdl",
                          &error);
    expect(error.empty(), "parses without an error");
    expect(doc.size() == 7, "top-level nodes, the slashdashed one left out");

    expect(kdl::number(doc, {"layout", "gaps"}) == -2.0, "a later section's value wins");
    expect(kdl::string(doc, {"layout", "center-focused-column"}) == "never", "a value only the first section has");
    expect(kdl::onOff(doc, {"layout", "border"}) == true, "on/off: the last one");
    expect(kdl::number(doc, {"layout", "border", "width"}) == 4.0, "semicolons inside a block");
    expect(!kdl::number(doc, {"layout", "struts", "left"}), "a slashdashed node");
    expect(kdl::number(doc, {"layout", "struts", "top"}) == 8.0 && !kdl::number(doc, {"layout", "struts", "bottom"}),
           "a block comment inside a line; the next node after it is an argument");
    const kdl::Node* top = kdl::last(doc, {"layout", "struts", "top"});
    expect(top && top->args.size() == 3 && top->args[2].number == 16, "hex numbers");

    const kdl::Node* match = kdl::last(doc, {"window-rule", "match"});
    expect(match && match->prop("app-id") && match->prop("app-id")->text == "^org\\.wezfurlong\\.wezterm$", "raw strings");
    expect(match && match->prop("title") && match->prop("title")->text == "a \"b\"\xC3\xA9", "escapes and \\u{}");
    expect(kdl::flag(doc, {"window-rule", "open-floating"}) == true, "a flag with true");
    expect(kdl::flag(doc, {"prefer-no-csd"}) == true, "a bare flag");
    expect(!kdl::flag(doc, {"focus-follows-mouse"}), "a flag that isn't there");
    const auto slow = kdl::number(doc, {"animations", "slowdown"});
    expect(slow && std::abs(*slow - 1.0005) < 1e-9, "underscores and exponents");
    expect(kdl::flag(doc, {"animations", "off"}) == true, "animations off");

    const kdl::Node* spawn = kdl::last(doc, {"spawn-at-startup"});
    expect(spawn && spawn->args.size() == 2 && spawn->args[1].text == "--flag", "a line continuation");
    const kdl::Node* ann = kdl::last(doc, {"annotated"});
    expect(ann && ann->args.size() == 3 && ann->args[0].type == kdl::Value::Type::Bool && ann->args[1].type == kdl::Value::Type::Null,
           "type annotations, version 2's #true and #null");

    // A syntax error: what came before it, and where
    auto broken = kdl::parse("cursor {\n    xcursor-size 24\n}\nlayout {\n    gaps 8\n", &error);
    expect(!error.empty() && broken.size() == 1 && broken[0].name == "cursor", "an unclosed block keeps what came before");
    kdl::parse("a \"open\n", &error);
    expect(error.starts_with("line "), "the error says where");
    expect(kdl::parse("", &error).empty() && error.empty(), "an empty file");

    // niri's own default config, when it's here
    const char* path = argc > 1 ? argv[1] : "/usr/share/doc/niri/default-config.kdl";
    if (std::ifstream in(path); in) {
        std::stringstream ss;
        ss << in.rdbuf();
        auto def = kdl::parse(ss.str(), &error);
        expect(error.empty(), "niri's default config parses");
        expect(kdl::number(def, {"layout", "gaps"}) == 16.0, "its gaps");
        expect(kdl::onOff(def, {"layout", "focus-ring"}) == std::nullopt || kdl::onOff(def, {"layout", "focus-ring"}) == true,
               "its focus ring");
        expect(kdl::last(def, {"binds"}) != nullptr, "its binds");
        if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
    }

    if (failures == 0) std::puts("kdl: all passed");
    return failures == 0 ? 0 : 1;
}
