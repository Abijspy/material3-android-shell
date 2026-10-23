// Quick Share's question and progress: who sends what, the PIN to compare
// with the phone's, Accept or Decline, then the files arriving. And sending:
// the files, the devices nearby, then the same progress the other way
#include "core/EventLoop.h"
#include "core/Process.h"
#include "quickshare/Qr.h"
#include "quickshare/QuickShare.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cmath>
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

// A QR code on a white card (cameras want the contrast), its eyes rounded
class QrView : public Node {
public:
    QrView(const std::string& text, float side) : m_modules(quickshare::qr::encode(text)), m_side(side) {}

protected:
    SizeF doMeasure(SizeF) override { return {m_side, m_side}; }
    void doPaint(Renderer& r) override {
        const RectF b = rect();
        r.rect(b, Color{1, 1, 1, 1}, Radii(24));
        const int n = static_cast<int>(m_modules.size());
        if (n == 0) return;
        // Whole device pixels per module, centred, a quiet zone around
        const float s = r.scale();
        const float m = std::floor(b.w * s / static_cast<float>(n + 6)) / s;
        const float x0 = std::round((b.x + (b.w - m * static_cast<float>(n)) / 2) * s) / s;
        const float y0 = std::round((b.y + (b.h - m * static_cast<float>(n)) / 2) * s) / s;
        const Color ink{0.11f, 0.11f, 0.13f, 1};
        const auto inEye = [n](int x, int y) { return (x < 7 && y < 7) || (x >= n - 7 && y < 7) || (x < 7 && y >= n - 7); };
        // Runs of dark modules along each row, one rect each
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n;) {
                if (!m_modules[static_cast<size_t>(y)][static_cast<size_t>(x)] || inEye(x, y)) {
                    ++x;
                    continue;
                }
                int end = x;
                while (end < n && m_modules[static_cast<size_t>(y)][static_cast<size_t>(end)] && !inEye(end, y)) ++end;
                r.rect({x0 + m * static_cast<float>(x), y0 + m * static_cast<float>(y), m * static_cast<float>(end - x), m}, ink);
                x = end;
            }
        for (const auto& [ex, ey] : {std::pair{0, 0}, std::pair{n - 7, 0}, std::pair{0, n - 7}}) {
            const float x = x0 + m * static_cast<float>(ex), y = y0 + m * static_cast<float>(ey);
            r.rect({x, y, m * 7, m * 7}, Color{1, 1, 1, 1}, Radii(m * 2.2f), m, ink);
            r.rect({x + m * 2, y + m * 2, m * 3, m * 3}, ink, Radii(m * 1.1f));
        }
    }

private:
    std::vector<std::vector<bool>> m_modules;
    float m_side;
};

} // namespace

void quickSharePanel(Flex* card, Popup& popup, const std::string& arg) {
    const int id = std::atoi(arg.c_str());
    auto* live = card->add<Live>();
    live->setSpacing(14)->setAlignItems(Align::Stretch);
    live->sync = [live, id, &popup] {
        live->clearChildren();
        const auto* t = QuickShare::get().transfer(id);
        if (!t) {
            caption(live, "That transfer is over")->setMaxWidth(480)->setMaxLines(3);
            return;
        }
        // Who, and the PIN to check against the phone's
        auto* head = live->add<Flex>(Flex::Direction::Row);
        head->setSpacing(14)->setAlignItems(Align::Center);
        auto* badge = head->add<Flex>(Flex::Direction::Row);
        badge->setJustify(Align::Center)->setRadius(Radii(18))->setBackground("primary_container", false);
        badge->fixedWidth = badge->fixedHeight = 52;
        badge->add<Icon>(t->outgoing ? "send" : "share", 28)->setFill(true)->setColor("on_primary_container", false);
        auto* titles = head->add<Flex>(Flex::Direction::Column);
        titles->setSpacing(2)->setAlignItems(Align::Start);
        titles->grow = true;
        titles->add<Label>(t->sender.empty() ? "A device nearby" : t->sender)->setSize(20)->setWeight(600)->setMaxWidth(320);
        const char* what = t->outgoing ? (t->state == QuickShare::State::Asking      ? "Waiting for them to accept…"
                                          : t->state == QuickShare::State::Receiving ? "Sending…"
                                          : t->state == QuickShare::State::Done      ? "Sent"
                                          : t->state == QuickShare::State::Declined  ? "Declined"
                                          : t->state == QuickShare::State::Failed    ? "Not sent"
                                                                                     : "Connecting…")
                         : t->state == QuickShare::State::Asking    ? "wants to share with you"
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

        if (t->outgoing && (t->state == QuickShare::State::Connecting || t->state == QuickShare::State::Asking)) {
            if (!t->pin.empty()) caption(live, "Check the PIN matches the one on the other device")->setMaxWidth(480)->setMaxLines(3);
            auto* foot = live->add<Flex>(Flex::Direction::Row);
            foot->setSpacing(8)->setJustify(Align::End);
            pillButton(foot, "Cancel", false, [id] { QuickShare::get().cancel(id); }, "close");
            return;
        }
        if (t->state == QuickShare::State::Asking) {
            caption(live, "Check the PIN matches the one on the other device. Files go to Downloads; links and text to the clipboard")->setMaxWidth(480)->setMaxLines(3);
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
            caption(live, std::format("{} of {} · {:.0f}%", size(t->received), size(t->total), done * 100))->setMaxWidth(480)->setMaxLines(3);
        }
        if (t->state == QuickShare::State::Failed && !t->error.empty()) caption(live, t->error)->setMaxWidth(480)->setMaxLines(3);
        auto* foot = live->add<Flex>(Flex::Direction::Row);
        foot->setSpacing(8)->setJustify(Align::End);
        if (t->outgoing && t->state == QuickShare::State::Receiving)
            pillButton(foot, "Cancel", false, [id] { QuickShare::get().cancel(id); }, "close");
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

void quickShareSend(Flex* card, Popup& popup, const std::string& arg) {
    struct State {
        std::vector<std::string> paths;
        QuickShare::Lease lease = QuickShare::get().discover();
        bool qr = false, sent = false;
    };
    auto st = std::make_shared<State>();
    for (size_t at = 0; at < arg.size();) {
        size_t nl = arg.find('\n', at);
        if (nl == std::string::npos) nl = arg.size();
        if (nl > at) st->paths.push_back(arg.substr(at, nl - at));
        at = nl + 1;
    }

    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(12)->setAlignItems(Align::Center);
    head->add<Icon>("send", 26)->setFill(true)->setColor("primary", false);
    auto* title = head->add<Label>("Quick Share");
    title->setSize(22)->setWeight(600);
    title->grow = true;
    iconButton(head, "close", [&popup] { popup.close(); });

    auto* live = card->add<Live>();
    live->setSpacing(14)->setAlignItems(Align::Stretch);
    live->sync = [live, st, &popup] {
        live->clearChildren();
        auto& qs = QuickShare::get();
        // What goes
        auto* files = live->add<Segments>();
        std::error_code ec;
        for (size_t i = 0; i < st->paths.size(); ++i) {
            const std::string& p = st->paths[i];
            const auto bytes = static_cast<int64_t>(std::filesystem::file_size(p, ec));
            auto r = row(files, "draft", std::filesystem::path(p).filename().string(), ec ? "Can't be read" : size(bytes));
            r.title->setMaxWidth(320);
            iconButton(r.row, "close", [st, i, live] {
                st->paths.erase(st->paths.begin() + static_cast<long>(i));
                live->syncSoon();
            });
        }
        auto* add = live->add<Flex>(Flex::Direction::Row);
        add->setSpacing(8);
        pillButton(add, st->paths.empty() ? "Choose files" : "Add files", st->paths.empty(), [st, &popup] {
            // The shell's own picker, these files kept; it comes back here
            std::string kept;
            for (const auto& p : st->paths) kept += p + "\n";
            popup.close();
            panels::open("files send\n" + kept);
        }, "add");

        const auto devices = qs.devices();
        // A phone that scanned the QR code: straight to it
        if (st->qr && !st->sent && !st->paths.empty())
            for (const auto& d : devices)
                if (d.scannedQr) {
                    st->sent = true;
                    const int id = qs.send(d, st->paths);
                    if (!id) break;
                    EventLoop::get().defer([&popup, id] {
                        popup.close();
                        panels::open("quickshare " + std::to_string(id));
                    });
                    return;
                }

        // The QR code, for the phone's camera
        if (st->qr) {
            auto* box = live->add<Flex>(Flex::Direction::Row);
            box->setSpacing(18)->setAlignItems(Align::Center)->setPadding({14, 14})->setRadius(Radii(28))->setBackground("surface_container_high", false);
            box->add<QrView>(qs.qrUrl(), 196);
            auto* side = box->add<Flex>(Flex::Direction::Column);
            side->setSpacing(8)->setAlignItems(Align::Start);
            side->grow = true;
            side->add<Label>("Scan with the phone's camera")->setSize(17)->setWeight(600)->setMaxWidth(270)->setMaxLines(2);
            side->add<Label>(st->paths.empty() ? "Choose files first: they go as soon as the phone connects"
                                               : "The files go as soon as the phone connects. No Bluetooth needed")
                ->setSize(13)
                ->setColor("on_surface_variant", false)
                ->setMaxWidth(270)
                ->setMaxLines(3);
            pillButton(side, "Hide", false, [st, live] {
                st->qr = false;
                live->syncSoon();
            }, "close");
        }

        // Who to
        auto* whoHead = live->add<Flex>(Flex::Direction::Row);
        whoHead->setAlignItems(Align::Center);
        auto* who = whoHead->add<Label>("Devices nearby");
        who->setSize(14)->setWeight(600)->setColor("primary", false);
        who->grow = true;
        if (!st->qr)
            pillButton(whoHead, "Use a QR code", false, [st, live] {
                st->qr = true;
                live->syncSoon();
            }, "qr_code_2");
        if (devices.empty()) {
            auto* wait = live->add<Flex>(Flex::Direction::Row);
            wait->setSpacing(12)->setAlignItems(Align::Center)->setPadding({4, 8});
            wait->add<Icon>("radar", 22)->setColor("on_surface_variant", false);
            wait->add<Label>("Looking for devices…")->setSize(15)->setColor("on_surface_variant", false);
        } else {
            auto* list = live->add<Segments>();
            for (const auto& d : devices) {
                const char* icon = d.type == 1 ? "smartphone" : d.type == 2 ? "tablet" : d.type == 3 ? "laptop" : "devices";
                auto r = row(list, icon, d.name, st->paths.empty() ? "Choose files first" : "");
                r.title->setMaxWidth(300);
                if (st->paths.empty()) continue;
                pillButton(r.row, "Send", true, [st, d, &popup] {
                    const int id = QuickShare::get().send(d, st->paths);
                    if (!id) return;
                    popup.close();
                    panels::open("quickshare " + std::to_string(id));
                }, "send");
            }
        }
        const std::string note = st->qr ? "" : qs.discoveryNote();
        caption(live, st->qr       ? "The phone needs to be on this Wi-Fi network"
                      : note.empty() ? "The phone needs to be on this Wi-Fi network. Not listed? Use a QR code, or set the phone's Quick Share to receive from everyone"
                                     : note + ". Or use a QR code")
            ->setMaxWidth(500)
            ->setMaxLines(3);
    };
    live->watch(QuickShare::get().changed);
    live->sync();
}

} // namespace m3::shell::panels
