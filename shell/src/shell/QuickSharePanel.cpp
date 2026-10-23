// Quick Share's question and progress: who sends what, the PIN to compare
// with the phone's, Accept or Decline, then the files arriving
#include "core/EventLoop.h"
#include "core/Process.h"
#include "quickshare/QuickShare.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;
using quickshare::QuickShare;

namespace {

std::string size(int64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.0f} KB", static_cast<double>(bytes) / 1024);
    if (bytes < 1024LL * 1024 * 1024) return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024 * 1024));
    return std::format("{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
}

std::string fileIcon(const std::string& mime) {
    if (mime.starts_with("image/")) return "image";
    if (mime.starts_with("video/")) return "movie";
    if (mime.starts_with("audio/")) return "music_note";
    if (mime == "application/pdf") return "picture_as_pdf";
    if (mime.find("zip") != std::string::npos || mime.find("tar") != std::string::npos) return "folder_zip";
    if (mime.starts_with("text/")) return "description";
    return "draft";
}

} // namespace

void quickSharePanel(Flex* card, Popup& popup, const std::string& arg) {
    const int id = std::atoi(arg.c_str());
    auto* live = card->add<Live>();
    live->setSpacing(14)->setAlignItems(Align::Stretch);
    live->sync = [live, id, &popup] {
        live->clearChildren();
        const auto* t = QuickShare::get().transfer(id);
        if (!t) {
            caption(live, "That transfer is over");
            return;
        }
        // Who, and the PIN to check against the phone's
        auto* head = live->add<Flex>(Flex::Direction::Row);
        head->setSpacing(14)->setAlignItems(Align::Center);
        auto* badge = head->add<Flex>(Flex::Direction::Row);
        badge->setJustify(Align::Center)->setRadius(Radii(18))->setBackground("primary_container", false);
        badge->fixedWidth = badge->fixedHeight = 52;
        badge->add<Icon>("share", 28)->setFill(true)->setColor("on_primary_container", false);
        auto* titles = head->add<Flex>(Flex::Direction::Column);
        titles->setSpacing(2)->setAlignItems(Align::Start);
        titles->grow = true;
        titles->add<Label>(t->sender.empty() ? "A device nearby" : t->sender)->setSize(20)->setWeight(600)->setMaxWidth(320);
        const char* what = t->state == QuickShare::State::Asking      ? "wants to share with you"
                         : t->state == QuickShare::State::Receiving ? "is sending…"
                         : t->state == QuickShare::State::Done      ? "sent"
                         : t->state == QuickShare::State::Declined  ? "was declined"
                         : t->state == QuickShare::State::Failed    ? "stopped"
                                                                    : "is connecting…";
        titles->add<Label>(what)->setSize(14)->setColor("on_surface_variant", false);
        if (!t->pin.empty()) {
            auto* pin = head->add<Flex>(Flex::Direction::Column);
            pin->setPadding({14, 6})->setRadius(Radii(16))->setBackground("secondary_container", false)->setAlignItems(Align::Center);
            pin->add<Label>("PIN")->setSize(11)->setWeight(600)->setColor("on_secondary_container", false);
            pin->add<Label>(t->pin)->setSize(20)->setWeight(700)->setColor("on_secondary_container", false);
        }

        // What
        auto* list = live->add<Segments>();
        for (const auto& f : t->files) {
            auto r = row(list, fileIcon(f.mime), f.name, size(f.size));
            r.title->setMaxWidth(340);
        }
        for (const auto& x : t->texts)
            row(list, x.type == 2 ? "link" : "notes", x.title.empty() ? "Text" : x.title, x.type == 2 ? "A link" : "Text");

        if (t->state == QuickShare::State::Asking) {
            caption(live, "Check the PIN matches the one on the other device. Files go to Downloads; links and text to the clipboard");
            auto* foot = live->add<Flex>(Flex::Direction::Row);
            foot->setSpacing(8)->setJustify(Align::End);
            pillButton(foot, "Decline", false, [id, &popup] {
                QuickShare::get().decline(id);
                popup.close();
            }, "close");
            pillButton(foot, "Accept", true, [id] { QuickShare::get().accept(id); }, "check");
            return;
        }
        if (t->state == QuickShare::State::Receiving && t->total > 0) {
            const float done = static_cast<float>(t->received) / static_cast<float>(t->total);
            live->add<Meter>(done, ColorRef("primary"), 10);
            caption(live, std::format("{} of {} · {:.0f}%", size(t->received), size(t->total), done * 100));
        }
        if (t->state == QuickShare::State::Failed && !t->error.empty()) caption(live, t->error);
        auto* foot = live->add<Flex>(Flex::Direction::Row);
        foot->setSpacing(8)->setJustify(Align::End);
        if (t->state == QuickShare::State::Done && !t->saved.empty()) {
            const std::string folder = std::filesystem::path(t->saved.front()).parent_path().string();
            pillButton(foot, "Show in folder", false, [folder, &popup] {
                spawn("xdg-open '" + folder + "'");
                popup.close();
            }, "folder_open");
        }
        pillButton(foot, t->state == QuickShare::State::Receiving ? "Hide" : "Close", true, [&popup] { popup.close(); });
    };
    live->watch(QuickShare::get().changed);
    live->sync();
}

} // namespace m3::shell::panels
