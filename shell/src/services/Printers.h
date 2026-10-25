#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <memory>
#include <string>
#include <vector>

namespace m3 {

namespace quickshare {
class MdnsBrowser;
}

// Printing through CUPS's own tools: the printers and their state, the
// queue, printers nearby (found with the shell's own mDNS browser, so
// systemd-resolved setups need no Avahi) or typed in by address, added
// driverless (IPP Everywhere), and printing files. Reading needs no root;
// adding, removing and pausing printers ask once (pkexec). Without CUPS:
// setUp() installs it.
class Printers {
public:
    struct Printer {
        std::string name, description, location, state; // state: idle, printing, stopped
        bool enabled = true, isDefault = false;
        std::string message; // what it says when stopped or in trouble
        bool operator==(const Printer&) const = default;
    };
    struct Job {
        std::string id, printer, user, title;
        long long size = 0;
        bool operator==(const Job&) const = default;
    };
    struct Found {
        std::string name, uri, model; // a printer nearby, not yet added
    };
    struct PrintOptions {
        int copies = 1;
        bool twoSided = false, colour = true, landscape = false;
        std::string pages; // "1-3,5"; empty: all
    };

    static Printers& get();

    bool installed() const { return m_installed; }  // CUPS's tools are here
    bool running() const { return m_running; }      // and the scheduler answers
    const std::vector<Printer>& printers() const { return m_printers; }
    const std::vector<Job>& jobs() const { return m_jobs; }
    const std::vector<Found>& found() const { return m_found; }
    bool searching() const { return m_searching; }
    const std::string& busy() const { return m_busy; } // what's being done now
    const std::string& lastError() const { return m_error; }

    void refresh();
    void search(); // printers nearby, for a few seconds
    void add(const Found& f);
    // One that doesn't announce itself: "192.168.1.20", "printer.lan:631"
    // or a whole "ipp://…" address
    void addAddress(const std::string& address);
    void remove(const std::string& printer);
    void setDefault(const std::string& printer);  // this user's default (lpoptions)
    void setEnabled(const std::string& printer, bool on);
    void testPage(const std::string& printer);
    void cancel(const std::string& jobId);
    void cancelAll();
    void print(const std::string& printer, const std::vector<std::string>& files, const PrintOptions& o);
    void setUp(); // installs CUPS, ipp-usb and the filters, starts it
    void start(); // installed but not running: starts it, and at boot

    // Watched (the queue, printers' states) every few seconds while held
    using Lease = std::shared_ptr<void>;
    [[nodiscard]] Lease watch();

    Signal<> changed;

    // Parsers, kept apart for the unit test
    static std::vector<Printer> parsePrinters(const std::string& lpstatP, const std::string& lpstatD);
    static std::vector<Job> parseJobs(const std::string& lpstatO);
    // "192.168.1.20" → "ipp://192.168.1.20/ipp/print"; "" when it can't be one
    static std::string ippAddress(const std::string& typed);

    ~Printers();

private:
    Printers();
    void asRoot(const std::string& what, const std::string& script);
    void admin(const std::string& what, const std::string& script); // as the user, else asRoot
    void done(const std::string& error);
    void collect(); // what the browser has found so far, less those added

    bool m_installed = false, m_running = false, m_searching = false;
    std::vector<Printer> m_printers;
    std::vector<Job> m_jobs;
    std::vector<Found> m_found;
    std::vector<std::string> m_addedUris; // device URIs set up already (lpstat -v)
    std::unique_ptr<quickshare::MdnsBrowser> m_browser;
    std::string m_busy, m_error;
    int m_watchers = 0;
    EventLoop::TimerId m_timer = 0, m_searchTimer = 0;
};

} // namespace m3
