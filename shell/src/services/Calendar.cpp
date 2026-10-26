#include "services/Calendar.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string dataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.local/share") + "/material3ui";
}

std::string cacheDir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.cache") + "/material3ui/calendar";
}

std::string newId() {
    static std::mt19937_64 rng{std::random_device{}()};
    return std::format("{:016x}", rng());
}

// ── ICS ──

// "20261023T100000Z" (UTC), "20261023T100000" (local, or in a TZID: read
// as local), "20261023" (a whole day)
std::time_t parseIcsTime(const std::string& value, bool& allDay) {
    allDay = value.size() == 8;
    if (value.size() < 8) return 0;
    std::tm tm{};
    try {
        tm.tm_year = std::stoi(value.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(value.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(value.substr(6, 2));
        if (!allDay && value.size() >= 15) {
            tm.tm_hour = std::stoi(value.substr(9, 2));
            tm.tm_min = std::stoi(value.substr(11, 2));
            tm.tm_sec = std::stoi(value.substr(13, 2));
        }
    } catch (...) {
        return 0;
    }
    if (!allDay && value.ends_with("Z")) return timegm(&tm);
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

// Text values: \n, \, and \; unescaped
std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            out += c == 'n' || c == 'N' ? '\n' : c;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::time_t startOfDay(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

// t moved on by days / months / years, at the same time of day
std::time_t addCalendar(std::time_t t, int days, int months, int years) {
    std::tm tm{};
    localtime_r(&t, &tm);
    tm.tm_mday += days;
    tm.tm_mon += months;
    tm.tm_year += years;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

nlohmann::json toJson(const Calendar::Event& e) {
    nlohmann::json j = {{"id", e.id},           {"title", e.title}, {"location", e.location}, {"notes", e.notes},
                        {"start", e.start},     {"end", e.end},     {"allDay", e.allDay},     {"repeat", e.repeat},
                        {"interval", e.interval}, {"count", e.count}, {"until", e.until},     {"byDay", e.byDay},
                        {"remind", e.remind}};
    j["except"] = std::vector<std::time_t>(e.except.begin(), e.except.end());
    return j;
}

Calendar::Event fromJson(const nlohmann::json& j) {
    Calendar::Event e;
    e.id = j.value("id", "");
    e.title = j.value("title", "");
    e.location = j.value("location", "");
    e.notes = j.value("notes", "");
    e.start = j.value("start", std::time_t{0});
    e.end = j.value("end", e.start);
    e.allDay = j.value("allDay", false);
    e.repeat = j.value("repeat", "");
    e.interval = std::max(1, j.value("interval", 1));
    e.count = j.value("count", 0);
    e.until = j.value("until", std::time_t{0});
    e.remind = j.value("remind", 10);
    if (j.contains("byDay") && j["byDay"].is_array())
        for (const auto& d : j["byDay"]) e.byDay.push_back(d.get<int>());
    if (j.contains("except") && j["except"].is_array())
        for (const auto& t : j["except"]) e.except.insert(t.get<std::time_t>());
    return e;
}

} // namespace

Calendar& Calendar::get() {
    static Calendar instance;
    return instance;
}

Calendar::Calendar() {
    load();
    loadSources();
    syncHolidays();
    // Subscriptions: from the cache at once, fresh soon after, then each half hour
    for (const auto& s : m_sources) {
        std::ifstream in(cacheDir() + "/" + s.id + ".ics");
        std::stringstream ss;
        ss << in.rdbuf();
        if (!ss.str().empty()) parseIcs(s.id, ss.str());
    }
    EventLoop::get().defer([this] { refresh(); });
    m_refreshTimer = EventLoop::get().addTimer(30 * 60 * 1000, [this] { refresh(); }, true);
    remindLoop();
}

// ── Your own events ──

void Calendar::load() {
    m_own.clear();
    std::ifstream in(dataDir() + "/calendar.json");
    if (!in) return;
    try {
        const auto j = nlohmann::json::parse(in);
        if (j.contains("events") && j["events"].is_array())
            for (const auto& e : j["events"]) m_own.push_back(fromJson(e));
    } catch (const std::exception& e) {
        log::warn("calendar.json: {}", e.what());
    }
}

void Calendar::store() const {
    nlohmann::json events = nlohmann::json::array();
    for (const auto& e : m_own) events.push_back(toJson(e));
    std::error_code ec;
    fs::create_directories(dataDir(), ec);
    const std::string file = dataDir() + "/calendar.json";
    std::ofstream(file + ".tmp") << nlohmann::json{{"events", events}}.dump(2);
    fs::rename(file + ".tmp", file, ec);
}

void Calendar::save(Event event) {
    event.source.clear();
    if (event.end < event.start) event.end = event.start;
    if (event.id.empty()) {
        event.id = newId();
        m_own.push_back(std::move(event));
    } else if (auto it = std::ranges::find(m_own, event.id, &Event::id); it != m_own.end()) {
        *it = std::move(event);
    } else {
        m_own.push_back(std::move(event));
    }
    store();
    changed.emit();
}

void Calendar::remove(const std::string& id) {
    std::erase_if(m_own, [&](const Event& e) { return e.id == id; });
    store();
    changed.emit();
}

const Calendar::Event* Calendar::find(const std::string& id) const {
    for (const auto* list : {&m_own, &m_subscribed})
        for (const auto& e : *list)
            if (e.id == id) return &e;
    return nullptr;
}

// ── Occurrences ──

std::vector<Calendar::Occurrence> Calendar::between(std::time_t from, std::time_t to) const {
    std::vector<Occurrence> out;
    auto enabled = [this](const Event& e) {
        if (e.source.empty() || isHoliday(e)) return true; // yours, and the holidays (there while a country's set)
        const auto it = std::ranges::find(m_sources, e.source, &Source::id);
        return it != m_sources.end() && it->enabled;
    };
    auto consider = [&](const Event& e) {
        if (!enabled(e)) return;
        const std::time_t length = std::max<std::time_t>(e.end - e.start, e.allDay ? 86400 : 1);
        auto take = [&](std::time_t start) {
            if (e.except.contains(start)) return;
            if (start + length > from && start < to) out.push_back({&e, start, start + length});
        };
        if (e.repeat.empty()) return take(e.start);
        // Walk the repeats up to the window's end
        int made = 0;
        std::time_t t = e.start;
        for (int step = 0; step < 20000 && t < to; ++step) {
            if (e.until && t > e.until) break;
            if (e.repeat == "weekly" && !e.byDay.empty()) {
                // Each chosen weekday, week by week from the start's week
                std::tm tm{};
                localtime_r(&t, &tm);
                const std::time_t weekStart = addCalendar(t, -tm.tm_wday, 0, 0);
                for (int d : e.byDay) {
                    if (e.count && made >= e.count) break;
                    const std::time_t day = addCalendar(weekStart, d, 0, 0);
                    if (day < e.start || (e.until && day > e.until)) continue;
                    ++made;
                    take(day);
                }
                if (e.count && made >= e.count) break;
                t = addCalendar(weekStart, 7 * e.interval, 0, 0);
                continue;
            }
            if (e.count && made >= e.count) break;
            ++made;
            take(t);
            if (e.repeat == "daily") t = addCalendar(t, e.interval, 0, 0);
            else if (e.repeat == "weekly") t = addCalendar(t, 7 * e.interval, 0, 0);
            else if (e.repeat == "monthly") t = addCalendar(e.start, 0, e.interval * made, 0);
            else if (e.repeat == "yearly") t = addCalendar(e.start, 0, 0, e.interval * made);
            else break;
        }
    };
    for (const auto& e : m_own) consider(e);
    for (const auto& e : m_subscribed) consider(e);
    // By day; within a day, all-day events first, then by time
    std::ranges::sort(out, [](const Occurrence& a, const Occurrence& b) {
        const std::time_t da = startOfDay(a.start), db = startOfDay(b.start);
        if (da != db) return da < db;
        if (a.event->allDay != b.event->allDay) return a.event->allDay;
        return a.start < b.start;
    });
    return out;
}

std::set<int> Calendar::busyDays(int year, int month) const {
    std::tm first{};
    first.tm_year = year - 1900;
    first.tm_mon = month;
    first.tm_mday = 1;
    first.tm_isdst = -1;
    const std::time_t from = std::mktime(&first);
    const std::time_t to = addCalendar(from, 0, 1, 0);
    std::set<int> days;
    for (const auto& o : between(from, to))
        // Every day it touches, within the month
        for (std::time_t d = std::max(startOfDay(o.start), from); d < std::min(o.end, to); d = addCalendar(d, 1, 0, 0)) {
            std::tm tm{};
            localtime_r(&d, &tm);
            days.insert(tm.tm_mday);
        }
    return days;
}

// ── Subscriptions ──

void Calendar::loadSources() {
    m_sources.clear();
    const auto j = Config::get().value<nlohmann::json>("/calendar/sources", nlohmann::json::array());
    if (!j.is_array()) return;
    for (const auto& s : j)
        m_sources.push_back({s.value("id", ""), s.value("name", "Calendar"), s.value("url", ""), s.value("colour", ""),
                             s.value("enabled", true), ""});
}

void Calendar::storeSources() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& s : m_sources)
        j.push_back({{"id", s.id}, {"name", s.name}, {"url", s.url}, {"colour", s.colour}, {"enabled", s.enabled}});
    Config::get().set("/calendar/sources", j);
}

void Calendar::subscribe(const std::string& name, const std::string& url) {
    std::string link = url;
    while (!link.empty() && (link.back() == ' ' || link.back() == '\n')) link.pop_back();
    // webcal:// is https
    if (link.starts_with("webcal://")) link = "https://" + link.substr(9);
    if (link.empty()) return;
    Source s{newId(), name.empty() ? "Calendar" : name, link, "", true, ""};
    m_sources.push_back(s);
    storeSources();
    fetch(s);
    changed.emit();
}

void Calendar::unsubscribe(const std::string& id) {
    std::erase_if(m_sources, [&](const Source& s) { return s.id == id; });
    std::erase_if(m_subscribed, [&](const Event& e) { return e.source == id; });
    std::error_code ec;
    fs::remove(cacheDir() + "/" + id + ".ics", ec);
    storeSources();
    changed.emit();
}

void Calendar::setEnabled(const std::string& id, bool on) {
    for (auto& s : m_sources)
        if (s.id == id) s.enabled = on;
    storeSources();
    changed.emit();
}

void Calendar::refresh() {
    for (const auto& s : m_sources) fetch(s);
    // Holidays change once a year or so: fetched again after a week
    std::error_code ec;
    const auto file = cacheDir() + "/" + kHolidays + ".ics";
    if (!m_holidays.url.empty() &&
        (!fs::exists(file, ec) || fs::file_time_type::clock::now() - fs::last_write_time(file, ec) > std::chrono::hours(24 * 7)))
        fetch(m_holidays);
}

const std::vector<Calendar::Country>& Calendar::holidayCountries() {
    // Google's public holiday calendars that answer (checked one by one)
    static const std::vector<Country> all = {
        {"en.ar", "Argentina", "AR"}, {"en.australian", "Australia", "AU"}, {"en.austrian", "Austria", "AT"},
        {"en.bd", "Bangladesh", "BD"}, {"en.be", "Belgium", "BE"}, {"en.brazilian", "Brazil", "BR"},
        {"en.canadian", "Canada", "CA"}, {"en.cl", "Chile", "CL"}, {"en.china", "China", "CN"},
        {"en.co", "Colombia", "CO"}, {"en.czech", "Czechia", "CZ"}, {"en.danish", "Denmark", "DK"},
        {"en.finnish", "Finland", "FI"}, {"en.french", "France", "FR"}, {"en.german", "Germany", "DE"},
        {"en.greek", "Greece", "GR"}, {"en.hong_kong", "Hong Kong", "HK"}, {"en.hungarian", "Hungary", "HU"},
        {"en.indian", "India", "IN"}, {"en.indonesian", "Indonesia", "ID"}, {"en.irish", "Ireland", "IE"},
        {"en.italian", "Italy", "IT"}, {"en.japanese", "Japan", "JP"}, {"en.malaysia", "Malaysia", "MY"},
        {"en.mexican", "Mexico", "MX"}, {"en.np", "Nepal", "NP"}, {"en.dutch", "Netherlands", "NL"},
        {"en.new_zealand", "New Zealand", "NZ"}, {"en.norwegian", "Norway", "NO"}, {"en.pk", "Pakistan", "PK"},
        {"en.pe", "Peru", "PE"}, {"en.philippines", "Philippines", "PH"}, {"en.polish", "Poland", "PL"},
        {"en.portuguese", "Portugal", "PT"}, {"en.romanian", "Romania", "RO"}, {"en.russian", "Russia", "RU"},
        {"en.saudiarabian", "Saudi Arabia", "SA"}, {"en.singapore", "Singapore", "SG"}, {"en.sa", "South Africa", "ZA"},
        {"en.south_korea", "South Korea", "KR"}, {"en.spain", "Spain", "ES"}, {"en.lk", "Sri Lanka", "LK"},
        {"en.swedish", "Sweden", "SE"}, {"en.taiwan", "Taiwan", "TW"}, {"en.turkish", "Turkey", "TR"},
        {"en.ukrainian", "Ukraine", "UA"}, {"en.ae", "United Arab Emirates", "AE"}, {"en.uk", "United Kingdom", "GB"},
        {"en.usa", "United States", "US"}, {"en.vietnamese", "Vietnam", "VN"},
    };
    return all;
}

std::string Calendar::holidayCountry() const {
    const auto j = Config::get().value<nlohmann::json>("/calendar/holidays", nlohmann::json());
    if (j.is_string()) return j.get<std::string>(); // "" when turned off
    // Unset: where the weather is ("Chennai, Tamil Nadu, India"), else the
    // language's region (en_IN)
    auto lower = [](std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::string place = Config::get().value<std::string>("/weather/location/name", "");
    if (const auto comma = place.rfind(','); comma != std::string::npos) place = place.substr(comma + 1);
    while (!place.empty() && place.front() == ' ') place.erase(0, 1);
    static const std::map<std::string, std::string> aliases = {
        {"united states of america", "united states"}, {"usa", "united states"}, {"uk", "united kingdom"},
        {"czech republic", "czechia"}, {"türkiye", "turkey"}, {"republic of korea", "south korea"}, {"korea", "south korea"}};
    if (const auto it = aliases.find(lower(place)); it != aliases.end()) place = it->second;
    for (const auto& c : holidayCountries())
        if (!place.empty() && lower(c.name) == lower(place)) return c.id;
    const char* lang = std::getenv("LANG");
    const std::string locale = lang ? lang : "";
    if (const auto us = locale.find('_'); us != std::string::npos && locale.size() >= us + 3)
        for (const auto& c : holidayCountries())
            if (locale.compare(us + 1, 2, c.code) == 0) return c.id;
    return "";
}

void Calendar::setHolidayCountry(const std::string& id) {
    Config::get().set("/calendar/holidays", id);
    std::error_code ec;
    fs::remove(cacheDir() + "/" + kHolidays + ".ics", ec); // another country's
    syncHolidays();
}

bool Calendar::observances() const {
    return Config::get().value<bool>("/calendar/observances", false);
}

void Calendar::setObservances(bool on) {
    Config::get().set("/calendar/observances", on);
    syncHolidays();
}

void Calendar::syncHolidays() {
    const std::string id = holidayCountry();
    std::erase_if(m_subscribed, [](const Event& e) { return isHoliday(e); });
    m_holidays = {};
    if (!id.empty()) {
        std::string name = id;
        for (const auto& c : holidayCountries())
            if (id == c.id) name = c.name;
        m_holidays = {kHolidays, "Holidays in " + name,
                      "https://calendar.google.com/calendar/ical/" + id + "%23holiday%40group.v.calendar.google.com/public/basic.ics", "", true, ""};
        std::ifstream in(cacheDir() + "/" + kHolidays + ".ics");
        std::stringstream ss;
        ss << in.rdbuf();
        if (!ss.str().empty()) parseIcs(kHolidays, ss.str());
        else fetch(m_holidays);
    }
    changed.emit();
}

std::map<int, std::string> Calendar::holidays(int year, int month) const {
    std::map<int, std::string> out;
    std::tm first{};
    first.tm_year = year - 1900;
    first.tm_mon = month;
    first.tm_mday = 1;
    first.tm_isdst = -1;
    const std::time_t from = std::mktime(&first);
    for (const auto& o : between(from, addCalendar(from, 0, 1, 0))) {
        if (!isHoliday(*o.event)) continue;
        std::tm tm{};
        localtime_r(&o.start, &tm);
        if (tm.tm_mon != month) continue;
        auto& name = out[tm.tm_mday];
        name += (name.empty() ? "" : ", ") + o.event->title;
    }
    return out;
}

void Calendar::fetch(const Source& s) {
    if (s.url.empty()) return;
    ++m_fetching;
    changed.emit();
    const std::string id = s.id;
    run({"curl", "-fsSL", "--max-time", "30", s.url}, [this, id](int code, const std::string& out) {
        --m_fetching;
        // A subscription, or the holidays (not one)
        Source* src = nullptr;
        if (id == kHolidays) {
            if (m_holidays.id == kHolidays) src = &m_holidays;
        } else if (auto it = std::ranges::find(m_sources, id, &Source::id); it != m_sources.end()) {
            src = &*it;
        }
        if (!src) return changed.emit(); // unsubscribed meanwhile
        if (code != 0 || out.find("BEGIN:VCALENDAR") == std::string::npos) {
            src->error = code != 0 ? "Couldn't reach it" : "Not a calendar: an ICS (iCal) link is needed";
            changed.emit();
            return;
        }
        src->error.clear();
        std::error_code ec;
        fs::create_directories(cacheDir(), ec);
        std::ofstream(cacheDir() + "/" + id + ".ics") << out;
        parseIcs(id, out);
        changed.emit();
    });
}

void Calendar::parseIcs(const std::string& sourceId, const std::string& text) {
    std::erase_if(m_subscribed, [&](const Event& e) { return e.source == sourceId; });
    // Unfold: a line starting with a space or tab continues the one before
    std::vector<std::string> lines;
    std::stringstream ss(text);
    for (std::string l; std::getline(ss, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty() && (l[0] == ' ' || l[0] == '\t') && !lines.empty()) lines.back() += l.substr(1);
        else lines.push_back(l);
    }
    const int remind = Config::get().value<int>("/calendar/remind", 10);
    Event e;
    bool inEvent = false;
    int depth = 0; // VALARM and the like, inside an event
    std::string uid;
    for (const auto& line : lines) {
        if (line == "BEGIN:VEVENT") {
            e = Event{};
            e.source = sourceId;
            e.remind = remind;
            uid.clear();
            inEvent = true;
            depth = 0;
            continue;
        }
        if (!inEvent) continue;
        if (line.starts_with("BEGIN:")) {
            ++depth;
            continue;
        }
        if (line.starts_with("END:") && line != "END:VEVENT" && depth > 0) {
            --depth;
            continue;
        }
        if (line == "END:VEVENT") {
            inEvent = false;
            if (e.start == 0) continue;
            if (e.end == 0) e.end = e.allDay ? e.start + 86400 : e.start;
            e.id = sourceId + ":" + (uid.empty() ? newId() : uid) + ":" + std::to_string(e.start);
            if (sourceId == kHolidays) {
                // Google's: "Public holiday", or "Observance\nTo hide observances…"
                const bool observance = e.notes.starts_with("Observance");
                if (observance && !observances()) continue;
                e.notes = observance ? "Observance" : "Public holiday";
                e.remind = -1;
            }
            m_subscribed.push_back(e);
            continue;
        }
        if (depth > 0) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        const std::string value = line.substr(colon + 1);
        const auto semi = name.find(';');
        const std::string params = semi == std::string::npos ? "" : name.substr(semi);
        if (semi != std::string::npos) name = name.substr(0, semi);
        bool allDay = false;
        if (name == "SUMMARY") e.title = unescape(value);
        else if (name == "LOCATION") e.location = unescape(value);
        else if (name == "DESCRIPTION") e.notes = unescape(value);
        else if (name == "UID") uid = value;
        else if (name == "DTSTART") {
            e.start = parseIcsTime(value, allDay);
            e.allDay = allDay || params.find("VALUE=DATE") != std::string::npos;
        } else if (name == "DTEND") {
            e.end = parseIcsTime(value, allDay);
        } else if (name == "EXDATE") {
            std::stringstream vs(value);
            for (std::string v; std::getline(vs, v, ',');) e.except.insert(parseIcsTime(v, allDay));
        } else if (name == "RRULE") {
            std::stringstream rs(value);
            for (std::string part; std::getline(rs, part, ';');) {
                const auto eq = part.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = part.substr(0, eq), v = part.substr(eq + 1);
                if (k == "FREQ") {
                    std::string f = v;
                    std::ranges::transform(f, f.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    e.repeat = f == "daily" || f == "weekly" || f == "monthly" || f == "yearly" ? f : "";
                } else if (k == "INTERVAL") e.interval = std::max(1, std::atoi(v.c_str()));
                else if (k == "COUNT") e.count = std::atoi(v.c_str());
                else if (k == "UNTIL") e.until = parseIcsTime(v, allDay) + (allDay ? 86399 : 0);
                else if (k == "BYDAY") {
                    static const char* names[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
                    std::stringstream ds(v);
                    for (std::string d; std::getline(ds, d, ',');)
                        for (int i = 0; i < 7; ++i)
                            if (d.ends_with(names[i])) e.byDay.push_back(i);
                }
            }
        }
    }
}

// ── Reminders ──

void Calendar::remindLoop() {
    // Each minute: whatever starts within its reminder, notified once
    const std::time_t now = std::time(nullptr);
    for (const auto& o : between(now - 60, now + 24 * 3600)) {
        const int remind = o.event->remind;
        if (remind < 0 || o.event->allDay) continue;
        if (o.start - remind * 60 > now || o.start < now - 60) continue;
        const std::string key = o.event->id + "@" + std::to_string(o.start);
        if (!m_reminded.insert(key).second) continue;
        std::tm tm{};
        localtime_r(&o.start, &tm);
        char at[32];
        std::strftime(at, sizeof at, "%H:%M", &tm);
        const long mins = std::max<long>(0, (o.start - now + 30) / 60);
        std::string body = mins == 0 ? std::string("Now · ") + at : std::format("In {} min · {}", mins, at);
        if (!o.event->location.empty()) body += " · " + o.event->location;
        run({"notify-send", "-a", "Calendar", "-i", "x-office-calendar", o.event->title.empty() ? "Event" : o.event->title, body}, nullptr);
    }
    m_remindTimer = EventLoop::get().addTimer(static_cast<int>((60 - now % 60) * 1000 + 200), [this] {
        m_remindTimer = 0;
        remindLoop();
    });
}

} // namespace m3
