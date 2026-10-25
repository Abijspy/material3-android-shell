#include "services/Printers.h"

#include "core/Process.h"
#include "quickshare/Mdns.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <filesystem>
#include <format>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

bool have(const char* tool) {
    std::error_code ec;
    return fs::exists(std::string("/usr/bin/") + tool, ec);
}

// CUPS's tools in English, whatever the session's language: parsed below
std::vector<std::string> c(std::vector<std::string> argv) {
    argv.insert(argv.begin(), {"env", "LC_ALL=C", "LANG=C"});
    return argv;
}

std::string quote(const std::string& s) {
    std::string out = "'";
    for (char ch : s) out += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
    return out + "'";
}

std::string lastLine(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    if (const auto nl = s.rfind('\n'); nl != std::string::npos) s = s.substr(nl + 1);
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

// A queue name CUPS takes: letters, digits, - and _
std::string queueName(const std::string& name) {
    std::string out;
    for (char ch : name) out += std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' ? ch : '_';
    while (out.find("__") != std::string::npos) out.replace(out.find("__"), 2, "_");
    if (out.size() > 60) out.resize(60);
    return out.empty() ? "Printer" : out;
}

} // namespace

Printers& Printers::get() {
    static Printers instance;
    return instance;
}

Printers::Printers() {
    refresh();
}

void Printers::refresh() {
    if (!have("lpstat")) {
        const bool was = m_installed || m_running || !m_printers.empty() || !m_jobs.empty();
        m_installed = m_running = false;
        m_printers.clear();
        m_jobs.clear();
        if (was) changed.emit();
        return;
    }
    if (!m_installed || m_converts != have("gs")) {
        m_installed = true; // just installed: the page moves on
        m_converts = have("gs");
        changed.emit();
    }
    // Is the scheduler there (lpstat -r), then its printers, default and queue
    // (and the queue's titles, which only lpq lists)
    run(c({"sh", "-c", "lpstat -r; echo '--'; lpstat -l -p 2>/dev/null; echo '--'; lpstat -d 2>/dev/null; echo '--'; lpstat -o 2>/dev/null; "
           "echo '--'; lpq -a 2>/dev/null"}),
        [this](int, const std::string& out) {
            std::vector<std::string> parts(1);
            std::stringstream ss(out);
            for (std::string l; std::getline(ss, l);) {
                if (l == "--") parts.emplace_back();
                else parts.back() += l + "\n";
            }
            parts.resize(5);
            const bool running = parts[0].find("is running") != std::string::npos;
            auto printers = parsePrinters(parts[1], parts[2]);
            auto jobs = parseJobs(parts[3]);
            parseTitles(parts[4], jobs);
            // Told only when something moved: this runs every few seconds
            // while watched, and pages rebuild on it
            if (running == m_running && printers == m_printers && jobs == m_jobs) return;
            m_running = running;
            m_printers = std::move(printers);
            m_jobs = std::move(jobs);
            changed.emit();
        });
}

Printers::~Printers() = default; // the browser's type is whole here

// Printers announce IPP over mDNS (_ipp._tcp, as IPP Everywhere, AirPrint
// and Mopria require). Looked for with the shell's own browser, the one
// Quick Share uses: no Avahi needed, and it lives beside systemd-resolved
void Printers::search() {
    if (m_searching) return;
    m_searching = true;
    m_found.clear();
    changed.emit();
    m_browser = std::make_unique<quickshare::MdnsBrowser>("_ipp._tcp");
    m_browser->changed = [this] { collect(); };
    // Six seconds: the browser asks twice more in that time
    m_searchTimer = EventLoop::get().addTimer(6000, [this] {
        m_searchTimer = 0;
        collect();
        m_browser.reset();
        m_searching = false;
        changed.emit();
    });
    // What's set up already, to leave out
    run(c({"lpstat", "-v"}), [this](int, const std::string& out) {
        m_addedUris.clear();
        std::stringstream ss(out);
        for (std::string l; std::getline(ss, l);)
            if (l.starts_with("device for "))
                if (const auto at = l.find(": "); at != std::string::npos) m_addedUris.push_back(trim(l.substr(at + 2)));
        collect();
    });
}

void Printers::collect() {
    if (!m_browser) return;
    std::vector<Found> found;
    for (const auto& [label, inst] : m_browser->instances()) {
        if (!inst.port || !inst.addr) continue; // not resolved yet
        // Its resource path (rp=, "ipp/print" by default) and model (ty=)
        std::string rp = "ipp/print", model;
        for (const auto& t : inst.txt) {
            if (t.starts_with("rp=")) rp = t.substr(3);
            else if (t.starts_with("ty=")) model = t.substr(3);
        }
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &inst.addr, ip, sizeof ip);
        Found f{label, std::format("ipp://{}:{}/{}", ip, inst.port, rp), model};
        const bool added = std::ranges::find(m_addedUris, f.uri) != m_addedUris.end() ||
                           std::ranges::any_of(m_printers, [&](const Printer& p) { return p.name == queueName(f.name) || p.description == f.name; });
        if (!added) found.push_back(std::move(f));
    }
    std::ranges::sort(found, {}, &Found::name);
    m_found = std::move(found);
    changed.emit();
}

void Printers::addAddress(const std::string& address) {
    const std::string uri = ippAddress(address);
    if (uri.empty()) {
        m_error = "That isn't a printer's address: try 192.168.1.20, or ipp://printer.local/ipp/print";
        changed.emit();
        return;
    }
    // Named for where it is: "Printer at 192.168.1.20"
    std::string host = uri.substr(uri.find("://") + 3);
    host = host.substr(0, host.find_first_of(":/"));
    add({"Printer at " + host, uri, ""});
}

void Printers::done(const std::string& error) {
    m_busy.clear();
    m_error = error;
    changed.emit(); // the busy line goes, the error shows, whatever refresh finds
    refresh();
}

void Printers::asRoot(const std::string& what, const std::string& script) {
    m_busy = what;
    m_error.clear();
    changed.emit();
    run({"pkexec", "sh", "-c", script}, [this](int code, const std::string& out) {
        // 126/127: the password prompt was dismissed
        done(code == 0 || code == 126 || code == 127 ? "" : lastLine(out).empty() ? "It didn't work" : lastLine(out));
    });
}

// CUPS's own administration (lpadmin, cupsenable…): tried as the user
// first, which CUPS allows its admin group (wheel on Arch) over its local
// socket, so most people never see a password prompt; refused, it asks once
void Printers::admin(const std::string& what, const std::string& script) {
    m_busy = what;
    m_error.clear();
    changed.emit();
    run(c({"sh", "-c", script}), [this, what, script](int code, const std::string& out) {
        if (code == 0) return done("");
        const bool refused = out.find("Forbidden") != std::string::npos || out.find("Unauthorized") != std::string::npos ||
                             out.find("not authorized") != std::string::npos || out.find("Password") != std::string::npos;
        if (refused) return asRoot(what, script);
        done(lastLine(out).empty() ? "It didn't work" : lastLine(out));
    });
}

void Printers::add(const Found& f) {
    const std::string name = queueName(f.name);
    // Driverless: the printer describes itself (IPP Everywhere); the first
    // one becomes the default
    std::string script = std::format("lpadmin -p {} -E -v {} -m everywhere -D {}", quote(name), quote(f.uri), quote(f.name));
    if (m_printers.empty()) script += " && lpadmin -d " + quote(name);
    std::erase_if(m_found, [&](const Found& o) { return o.uri == f.uri; });
    admin("Adding " + f.name + "…", script);
}

void Printers::remove(const std::string& printer) {
    admin("Removing " + printer + "…", "lpadmin -x " + quote(printer));
}

void Printers::setDefault(const std::string& printer) {
    run(c({"lpoptions", "-d", printer}), [this](int code, const std::string& out) { done(code == 0 ? "" : lastLine(out)); });
}

void Printers::setEnabled(const std::string& printer, bool on) {
    admin(on ? "Resuming…" : "Pausing…", (on ? "cupsenable " : "cupsdisable ") + quote(printer) + (on ? " && cupsaccept " + quote(printer) : ""));
}

void Printers::testPage(const std::string& printer) {
    const char* page = "/usr/share/cups/data/testprint";
    run(c({"lp", "-d", printer, "-t", "Test page", page}), [this, printer](int code, const std::string& out) {
        if (code == 0) run({"notify-send", "-a", "Printers", "-i", "printer", "Test page sent", printer}, nullptr);
        done(code == 0 ? "" : lastLine(out));
    });
}

void Printers::cancel(const std::string& jobId) {
    run(c({"cancel", jobId}), [this](int code, const std::string& out) { done(code == 0 ? "" : lastLine(out)); });
}

void Printers::cancelAll() {
    run(c({"cancel", "-a"}), [this](int code, const std::string& out) { done(code == 0 ? "" : lastLine(out)); });
}

void Printers::print(const std::string& printer, const std::vector<std::string>& files, const PrintOptions& o) {
    std::vector<std::string> argv = {"lp", "-d", printer, "-n", std::to_string(std::clamp(o.copies, 1, 99))};
    if (o.twoSided) argv.insert(argv.end(), {"-o", o.landscape ? "sides=two-sided-short-edge" : "sides=two-sided-long-edge"});
    if (!o.colour) argv.insert(argv.end(), {"-o", "print-color-mode=monochrome"});
    if (o.landscape) argv.insert(argv.end(), {"-o", "landscape"});
    if (!o.pages.empty()) argv.insert(argv.end(), {"-P", o.pages});
    argv.insert(argv.end(), files.begin(), files.end());
    const std::string what = files.size() == 1 ? fs::path(files.front()).filename().string() : std::format("{} files", files.size());
    run(c(argv), [this, printer, what](int code, const std::string& out) {
        run({"notify-send", "-a", "Printers", "-i", "printer", code == 0 ? "Printing" : "Couldn't print",
             code == 0 ? what + " · " + printer : lastLine(out)},
            nullptr);
        done(code == 0 ? "" : lastLine(out));
    });
}

void Printers::setUp() {
    asRoot("Setting up printing…",
           "pacman -S --needed --noconfirm cups cups-filters ghostscript ipp-usb && systemctl enable --now cups.socket cups.service");
}

void Printers::installConverter() {
    asRoot("Installing Ghostscript…", "pacman -S --needed --noconfirm ghostscript");
}

void Printers::start() {
    asRoot("Starting the print service…", "systemctl enable --now cups.socket cups.service");
}

Printers::Lease Printers::watch() {
    if (m_watchers++ == 0) {
        refresh();
        m_timer = EventLoop::get().addTimer(3000, [this] { refresh(); }, true);
    }
    return std::shared_ptr<void>(nullptr, [](void*) {
        auto& p = Printers::get();
        if (--p.m_watchers == 0 && p.m_timer) {
            EventLoop::get().cancel(p.m_timer);
            p.m_timer = 0;
        }
    });
}

} // namespace m3
