// Alarms' and timers' pure parts, apart for the unit tests: when an alarm
// comes round, how its days and lengths read and parse
#include "services/Alarms.h"

#include <algorithm>
#include <cctype>
#include <format>

#include <sstream>

namespace m3 {

std::time_t Alarms::next(const Alarm& a, std::time_t from) {
    if (!a.enabled) return 0;
    std::tm tm{};
    localtime_r(&from, &tm);
    for (int d = 0; d <= 7; ++d) {
        std::tm c = tm;
        c.tm_mday += d;
        c.tm_hour = a.hour;
        c.tm_min = a.minute;
        c.tm_sec = 0;
        c.tm_isdst = -1;
        const std::time_t at = std::mktime(&c); // and c.tm_wday, that day's
        if (at <= from) continue;
        if (!a.days.empty() && std::ranges::find(a.days, c.tm_wday) == a.days.end()) continue;
        return at;
    }
    return 0;
}

std::string Alarms::daysText(const Alarm& a) {
    if (a.days.empty()) {
        // Once: whichever of today and tomorrow it comes round on
        Alarm on = a;
        on.enabled = true;
        const std::time_t now = std::time(nullptr), at = next(on, now);
        std::tm n{}, t{};
        localtime_r(&now, &n);
        localtime_r(&at, &t);
        return n.tm_yday == t.tm_yday ? "Today" : "Tomorrow";
    }
    if (a.days.size() == 7) return "Every day";
    if (a.days == std::vector<int>{1, 2, 3, 4, 5}) return "Weekdays";
    if (a.days == std::vector<int>{0, 6}) return "Weekends";
    static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    std::string out;
    for (int d : {1, 2, 3, 4, 5, 6, 0}) // the week from Monday
        if (std::ranges::find(a.days, d) != a.days.end()) out += (out.empty() ? "" : ", ") + std::string(names[d]);
    return out;
}

std::string Alarms::untilText(std::time_t when) {
    const long secs = static_cast<long>(when - std::time(nullptr));
    if (secs < 30) return "Now";
    const long mins = (secs + 59) / 60, h = mins / 60, m = mins % 60;
    if (h >= 24) return std::format("In {} d {} h", h / 24, h % 24);
    if (h == 0) return std::format("In {} min", m);
    return m ? std::format("In {} h {} min", h, m) : std::format("In {} h", h);
}

int64_t Alarms::parseLength(const std::string& input) {
    std::string text;
    for (char c : input)
        if (c != ' ') text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (text.empty()) return 0;
    int64_t total = 0;
    try {
        if (text.find_first_of("hms") != std::string::npos) {
            // "1h20m", "90s", "1h 30"
            size_t i = 0;
            while (i < text.size()) {
                size_t used = 0;
                const double n = std::stod(text.substr(i), &used);
                i += used;
                const char unit = i < text.size() ? text[i] : 'm';
                if (unit != 'h' && unit != 'm' && unit != 's') return 0;
                total += static_cast<int64_t>(n * (unit == 'h' ? 3600000 : unit == 'm' ? 60000 : 1000));
                ++i;
                if (i < text.size() && text.compare(i, 2, "in") == 0) i += 2; // "min"
                if (i < text.size() && (text[i] == 'r' || text[i] == 'c')) ++i; // "hr", "sec"
            }
        } else if (text.find(':') != std::string::npos) {
            // "m:ss", "h:mm:ss"
            std::vector<int64_t> parts;
            size_t start = 0;
            for (;;) {
                const size_t colon = text.find(':', start);
                parts.push_back(std::stoll(text.substr(start, colon - start)));
                if (colon == std::string::npos) break;
                start = colon + 1;
            }
            if (parts.size() > 3) return 0;
            for (int64_t p : parts) total = total * 60 + p;
            total *= 1000;
        } else {
            // A number alone: minutes
            size_t used = 0;
            const double n = std::stod(text, &used);
            if (used != text.size()) return 0;
            total = static_cast<int64_t>(n * 60000);
        }
    } catch (...) {
        return 0;
    }
    return total > 0 && total <= 100LL * 3600000 ? total : 0;
}

bool Alarms::parseTimerQuery(const std::string& input, int64_t& ms, std::string& label) {
    std::string q;
    for (char c : input) q += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!q.empty() && q.back() == ' ') q.pop_back();
    q.erase(0, q.find_first_not_of(' '));
    // Words for units as parseLength reads them ("1 hour and 30 minutes" → "1h30m")
    auto length = [](std::string t) -> int64_t {
        std::string out;
        std::stringstream ss(t);
        for (std::string w; ss >> w;) {
            if (w == "and" || w == "a" || w == "an") {
                if (w != "and") out += "1";
                continue;
            }
            if (w == "hour" || w == "hours" || w == "hr" || w == "hrs") w = "h";
            else if (w == "minute" || w == "minutes" || w == "mins" || w == "min") w = "m";
            else if (w == "second" || w == "seconds" || w == "secs" || w == "sec") w = "s";
            else if (w == "half" && !out.empty() && out.back() == 'h') continue;
            out += w;
        }
        return out.empty() || out.find_first_of("0123456789") == std::string::npos ? 0 : parseLength(out);
    };
    label.clear();
    std::string rest;
    if (q.starts_with("remind me ")) {
        rest = q.substr(10);
        // "in <length> to <what>" or "to <what> in <length>"
        if (rest.starts_with("in ")) {
            const auto to = rest.find(" to ");
            ms = length(rest.substr(3, to == std::string::npos ? std::string::npos : to - 3));
            if (to != std::string::npos) label = input.substr(input.size() - (rest.size() - to - 4));
        } else if (rest.starts_with("to ")) {
            const auto in = rest.rfind(" in ");
            if (in == std::string::npos) return false;
            ms = length(rest.substr(in + 4));
            label = input.substr(input.size() - rest.size() + 3, in - 3);
        } else {
            return false;
        }
        if (label.empty()) label = "Reminder";
        return ms > 0;
    }
    if (q.starts_with("timer for ")) rest = q.substr(10);
    else if (q.starts_with("timer ")) rest = q.substr(6);
    else if (q.ends_with(" timer")) rest = q.substr(0, q.size() - 6);
    else return false;
    ms = length(rest);
    return ms > 0;
}

std::string Alarms::countdown(int64_t ms) {
    const int64_t s = (std::max<int64_t>(0, ms) + 999) / 1000; // 0.4 s to go still reads 0:01
    const int64_t h = s / 3600, m = s / 60 % 60, sec = s % 60;
    return h ? std::format("{}:{:02}:{:02}", h, m, sec) : std::format("{}:{:02}", m, sec);
}

} // namespace m3
