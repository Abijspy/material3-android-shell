// Settings → Printers, and the print dialog: CUPS's printers, those nearby
// to add (driverless), the queue; and printing files with a printer,
// copies, pages, two-sided, colour and orientation
#include "shell/PrintersPage.h"

#include "services/Printers.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"

#include <filesystem>
#include <format>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

std::string sizeText(long long bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{} KB", bytes / 1024);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024));
}

std::string stateText(const Printers::Printer& p) {
    std::string s = p.state == "printing" ? "Printing…" : p.state == "stopped" ? "Paused" : "Ready";
    if (p.state == "stopped" && !p.message.empty()) s += ": " + p.message;
    if (p.isDefault) s += " · Default";
    if (!p.location.empty()) s += " · " + p.location;
    return s;
}

} // namespace

void printersPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(0)->setAlignItems(Align::Stretch);
    auto lease = std::make_shared<Printers::Lease>(Printers::get().watch());
    // An address being typed, kept when the page rebuilds (a job arriving)
    auto typed = std::make_shared<std::string>();
    auto searched = std::make_shared<bool>(false); // a search has run: "none found" means something
    live->sync = [live, lease, typed, searched] {
        (void)lease;
        live->clearChildren();
        auto& pr = Printers::get();
        auto error = [&] {
            if (pr.lastError().empty()) return;
            auto* l = live->add<Label>(pr.lastError());
            l->setSize(13)->setColor("error", false)->setMaxWidth(640)->setMaxLines(3);
        };

        // Not there yet: set it up, or start it
        if (!pr.installed() || !pr.running()) {
            auto* g = group(live, "Printing", "Printers on your network and over USB, most without a driver (IPP Everywhere)");
            auto r = row(g, "print_disabled", pr.installed() ? "The print service isn't running" : "Printing isn't set up",
                         pr.installed() ? "Start CUPS, now and at every boot" : "Installs CUPS, its filters and ipp-usb, and starts it");
            pillButton(r.row, !pr.busy().empty() ? "Working…" : pr.installed() ? "Start" : "Set up", true,
                       [installed = pr.installed()] {
                           if (installed) Printers::get().start();
                           else Printers::get().setUp();
                       },
                       pr.installed() ? "play_arrow" : "download");
            error();
            return;
        }

        // Your printers
        auto* mine = group(live, "Printers", pr.printers().empty() ? "None yet: add one from those nearby" : "");
        for (const auto& p : pr.printers()) {
            auto r = row(mine, p.state == "stopped" ? "print_disabled" : p.state == "printing" ? "print_connect" : "print",
                         p.description.empty() ? p.name : p.description, stateText(p));
            r.title->setMaxWidth(260);
            if (p.state == "stopped") r.subtitle->setColor("error", false);
            const std::string name = p.name;
            if (!p.isDefault) pillButton(r.row, "Make default", false, [name] { Printers::get().setDefault(name); });
            iconButton(r.row, "description", [name] { Printers::get().testPage(name); })->tooltip = "Print a test page";
            const bool on = p.enabled;
            iconButton(r.row, on ? "pause" : "play_arrow", [name, on] { Printers::get().setEnabled(name, !on); })->tooltip =
                on ? "Pause" : "Resume";
            confirmButton(r.row, "printer:" + name, "delete", [name] { Printers::get().remove(name); });
        }
        if (!pr.busy().empty()) caption(live, pr.busy());
        error();

        // Nearby: on the network, or over USB
        auto* near = group(live, "Add a printer", "Printers that describe themselves (IPP Everywhere, AirPrint, Mopria): no driver needed");
        auto find = row(near, "search", pr.searching() ? "Looking…" : "Find printers", "On this network, and USB printers shared by ipp-usb");
        pillButton(find.row, pr.searching() ? "Looking…" : "Search", !pr.searching(), [searched] {
            *searched = true;
            Printers::get().search();
        }, "refresh");
        for (const auto& f : pr.found()) {
            auto r = row(near, "print_add", f.name, f.model.empty() ? f.uri : f.model);
            r.title->setMaxWidth(300);
            const auto found = f;
            pillButton(r.row, "Add", true, [found] { Printers::get().add(found); }, "add");
        }
        if (*searched && !pr.searching() && pr.found().empty()) {
            auto r = row(near, "print_disabled", "None found", "Is it on, and on this Wi-Fi? Or add it by its address below");
            r.title->setColor("on_surface_variant", false);
        }
        // One that doesn't announce itself: by its IP or name
        auto byAddress = row(near, "lan", "Add by address", "Its IP, from the printer's own menu");
        auto* field = byAddress.row->add<TextField>();
        field->fixedWidth = 190;
        field->setPlaceholder("192.168.1.20");
        field->setText(*typed);
        field->onChanged = [typed](const std::string& t) { *typed = t; };
        field->onSubmit = [typed](const std::string& t) {
            if (!t.empty()) Printers::get().addAddress(t);
            typed->clear();
        };
        pillButton(byAddress.row, "Add", false, [typed] {
            if (typed->empty()) return;
            Printers::get().addAddress(*typed);
            typed->clear();
        }, "add");

        // The queue
        auto* queue = group(live, "Print queue", pr.jobs().empty() ? "Nothing waiting" : "");
        for (const auto& j : pr.jobs()) {
            auto r = row(queue, "description", std::format("Job {}", j.id.substr(j.id.rfind('-') + 1)),
                         std::format("{} · {} · {}", j.printer, j.user, sizeText(j.size)));
            const std::string id = j.id;
            iconButton(r.row, "close", [id] { Printers::get().cancel(id); })->tooltip = "Cancel";
        }
        if (pr.jobs().size() > 1) {
            auto all = row(queue, "clear_all", "Cancel every job", "");
            pillButton(all.row, "Cancel all", false, [] { Printers::get().cancelAll(); });
        }
    };
    live->watch(Printers::get().changed);
    live->sync();
}

void printDialog(Flex* card, Popup& popup, const std::string& arg) {
    struct State {
        std::vector<std::string> files;
        std::string printer;
        Printers::PrintOptions options;
        Printers::Lease lease = Printers::get().watch();
        bool built = false; // the form, once the printers are known
    };
    auto st = std::make_shared<State>();
    for (size_t at = 0; at < arg.size();) {
        size_t nl = arg.find('\n', at);
        if (nl == std::string::npos) nl = arg.size();
        if (nl > at) st->files.push_back(arg.substr(at, nl - at));
        at = nl + 1;
    }
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(12)->setAlignItems(Align::Center);
    head->add<Icon>("print", 26)->setColor("primary", false);
    auto* title = head->add<Label>("Print");
    title->setSize(22)->setWeight(600);
    title->grow = true;
    iconButton(head, "close", [&popup] { popup.close(); });

    auto* live = card->add<Live>();
    live->setSpacing(12)->setAlignItems(Align::Stretch);
    live->sync = [live, st, &popup] {
        auto& pr = Printers::get();
        const bool ready = pr.installed() && pr.running() && !pr.printers().empty();
        // Built once when ready: the queue moving every few seconds mustn't
        // rebuild the form under the pointer (and close the dropdown)
        if (st->built && ready) return;
        live->clearChildren();
        // What prints
        auto* files = live->add<Segments>();
        for (const auto& f : st->files) row(files, "draft", std::filesystem::path(f).filename().string(), "").title->setMaxWidth(360);
        if (st->files.empty()) caption(live, "Nothing to print: open a file with Print, or run material3ui msg print <file>")->setMaxWidth(440);
        if (!ready) {
            caption(live, !pr.installed() ? "Printing isn't set up yet" : !pr.running() ? "The print service isn't running" : "No printers yet")
                ->setMaxWidth(440);
            pillButton(live, "Printer settings", true, [&popup] {
                popup.close();
                if (open) open("settings printers"); // opens Settings too, if it's closed
            }, "settings");
            return;
        }
        st->built = true;
        // The printer: the default unless chosen
        if (st->printer.empty())
            for (const auto& p : pr.printers())
                if (p.isDefault || st->printer.empty()) st->printer = p.name;
        auto* g = live->add<Segments>();
        auto* pick = g->add<Flex>(Flex::Direction::Column);
        pick->setAlignItems(Align::Stretch);
        auto which = row(pick, "print", "Printer", "");
        std::vector<std::pair<std::string, std::string>> names;
        std::string shown = st->printer;
        for (const auto& p : pr.printers()) {
            const std::string label = p.description.empty() ? p.name : p.description;
            names.push_back({p.name, label});
            if (p.name == st->printer) shown = label;
        }
        dropdown(which.row, pick, shown, names, [st](const std::string& v) { st->printer = v; });
        // Copies: − n +
        auto copies = row(g, "content_copy", "Copies", "");
        auto* count = copies.row->add<Label>(std::to_string(st->options.copies));
        count->setSize(16)->setWeight(600);
        iconButton(copies.row, "remove", [st, count] {
            st->options.copies = std::max(1, st->options.copies - 1);
            count->setText(std::to_string(st->options.copies));
        });
        iconButton(copies.row, "add", [st, count] {
            st->options.copies = std::min(99, st->options.copies + 1);
            count->setText(std::to_string(st->options.copies));
        });
        // Pages: all, or a range
        auto pages = row(g, "article", "Pages", "All, or a range such as 1-3,5");
        auto* range = pages.row->add<TextField>();
        range->fixedWidth = 150;
        range->setPlaceholder("All");
        range->setText(st->options.pages);
        range->onChanged = [st](const std::string& t) { st->options.pages = t; };
        auto toggle = [&](const char* icon, const char* name, bool Printers::PrintOptions::*field) {
            auto r = row(g, icon, name, "");
            auto* s = r.row->add<Switch>();
            s->setChecked(st->options.*field, false);
            s->onToggled = [st, field](bool on) { st->options.*field = on; };
        };
        toggle("auto_stories", "Two-sided", &Printers::PrintOptions::twoSided);
        toggle("palette", "Colour", &Printers::PrintOptions::colour);
        toggle("screen_rotation", "Landscape", &Printers::PrintOptions::landscape);

        auto* foot = live->add<Flex>(Flex::Direction::Row);
        foot->setSpacing(8)->setJustify(Align::End);
        pillButton(foot, "Cancel", false, [&popup] { popup.close(); });
        pillButton(foot, "Print", true, [st, &popup] {
            if (st->files.empty() || st->printer.empty()) return;
            Printers::get().print(st->printer, st->files, st->options);
            popup.close();
        }, "print");
    };
    live->watch(Printers::get().changed);
    live->sync();
}

} // namespace m3::shell::panels
