// Mail (material3ui-mail): one email account over IMAP and SMTP, through
// curl. The password stays in your keyring (the Secret Service) and reaches
// curl in a file only you can read, never on a command line. Folders, the
// latest messages, reading (plain text preferred, HTML as text), replying,
// forwarding, writing, deleting and attachments.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/FileOps.h"
#include "services/Secrets.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/TextArea.h"
#include "ui/Theme.h"

#include <glib.h>
#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

std::function<void(const std::string&)> mailGo;

namespace {

// ── MIME, as much of it as mail in the wild needs

std::string lowerOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}
std::string toUtf8(const std::string& text, std::string charset) {
    charset = lowerOf(trim(charset));
    if (charset.empty() || charset == "utf-8" || charset == "utf8" || charset == "us-ascii") {
        if (g_utf8_validate(text.c_str(), static_cast<gssize>(text.size()), nullptr)) return text;
        charset = "windows-1252";
    }
    gsize n = 0;
    gchar* out = g_convert_with_fallback(text.c_str(), static_cast<gssize>(text.size()), "UTF-8", charset.c_str(), "?", nullptr, &n, nullptr);
    if (!out) return text;
    std::string s(out, n);
    g_free(out);
    return s;
}
std::string decodeB64(const std::string& s) {
    std::string clean;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) clean += c;
    gsize n = 0;
    guchar* b = g_base64_decode(clean.c_str(), &n);
    std::string out(reinterpret_cast<char*>(b), n);
    g_free(b);
    return out;
}
std::string decodeQP(const std::string& s, bool header = false) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (header && s[i] == '_') out += ' ';
        else if (s[i] == '=' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else if (s[i] == '=' && i + 1 < s.size() && (s[i + 1] == '\r' || s[i + 1] == '\n')) {
            i += s[i + 1] == '\r' && i + 2 < s.size() && s[i + 2] == '\n' ? 2 : 1; // a soft break
        } else out += s[i];
    }
    return out;
}
// =?charset?B|Q?…?= words in a header
std::string decodeWords(const std::string& h) {
    static const std::regex word(R"(=\?([^?]+)\?([BbQq])\?([^?]*)\?=)");
    std::string out;
    auto it = std::sregex_iterator(h.begin(), h.end(), word);
    size_t last = 0;
    for (; it != std::sregex_iterator(); ++it) {
        std::string between = h.substr(last, static_cast<size_t>(it->position()) - last);
        if (last > 0 && trim(between).empty()) between.clear(); // spaces between two words go
        out += between;
        const std::string raw = (*it)[2].str() == "B" || (*it)[2].str() == "b" ? decodeB64((*it)[3].str()) : decodeQP((*it)[3].str(), true);
        out += toUtf8(raw, (*it)[1].str());
        last = static_cast<size_t>(it->position() + it->length());
    }
    out += h.substr(last);
    return toUtf8(out, "utf-8");
}
// Headers: unfolded, by lower-case name (the first of each)
std::map<std::string, std::string> parseHeaders(const std::string& block) {
    std::map<std::string, std::string> h;
    std::string cur;
    std::istringstream in(block);
    auto flush = [&] {
        const size_t c = cur.find(':');
        if (c != std::string::npos) {
            const std::string k = lowerOf(trim(cur.substr(0, c)));
            if (!h.contains(k)) h[k] = trim(cur.substr(c + 1));
        }
        cur.clear();
    };
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty() && (l[0] == ' ' || l[0] == '\t')) cur += " " + trim(l);
        else flush(), cur = l;
    }
    flush();
    return h;
}
std::string param(const std::string& value, const std::string& name) {
    const std::string low = lowerOf(value);
    size_t at = 0;
    while ((at = low.find(name, at)) != std::string::npos) {
        if ((at == 0 || low[at - 1] == ';' || low[at - 1] == ' ' || low[at - 1] == '\t') && at + name.size() < low.size()) {
            size_t v = at + name.size();
            if (low[v] == '*') v = low.find('=', v); // name*=utf-8''…
            if (v != std::string::npos && low[v] == '=') {
                ++v;
                std::string out;
                if (v < value.size() && value[v] == '"') {
                    const size_t e = value.find('"', v + 1);
                    out = value.substr(v + 1, e == std::string::npos ? std::string::npos : e - v - 1);
                } else {
                    const size_t e = value.find(';', v);
                    out = trim(value.substr(v, e == std::string::npos ? std::string::npos : e - v));
                }
                if (const size_t q = out.find("''"); q != std::string::npos && q < 20) {
                    gchar* u = g_uri_unescape_string(out.substr(q + 2).c_str(), nullptr);
                    if (u) out = u, g_free(u);
                }
                return decodeWords(out);
            }
        }
        at += name.size();
    }
    return "";
}
std::string htmlToText(const std::string& html) {
    std::string out;
    std::string tag;
    bool inTag = false, skip = false;
    for (size_t i = 0; i < html.size(); ++i) {
        const char c = html[i];
        if (c == '<') {
            inTag = true;
            tag.clear();
            continue;
        }
        if (inTag) {
            if (c == '>') {
                inTag = false;
                const std::string t = lowerOf(tag);
                if (t.starts_with("style") || t.starts_with("script") || t.starts_with("head")) skip = true;
                else if (t.starts_with("/style") || t.starts_with("/script") || t.starts_with("/head")) skip = false;
                if (t == "br" || t == "br/" || t == "br /" || t.starts_with("/p") || t.starts_with("/div") || t.starts_with("/tr") || t.starts_with("/h") || t.starts_with("li"))
                    out += '\n';
                if (t.starts_with("li")) out += "• ";
            } else {
                tag += c;
            }
            continue;
        }
        if (!skip) out += (c == '\r' || c == '\t') ? ' ' : c == '\n' ? ' ' : c;
    }
    // Entities, then blank lines squeezed
    std::string dec;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '&') {
            const size_t semi = out.find(';', i);
            if (semi != std::string::npos && semi - i < 10) {
                const std::string e = out.substr(i + 1, semi - i - 1);
                std::string r;
                if (e == "amp") r = "&";
                else if (e == "lt") r = "<";
                else if (e == "gt") r = ">";
                else if (e == "quot") r = "\"";
                else if (e == "apos") r = "'";
                else if (e == "nbsp") r = " ";
                else if (!e.empty() && e[0] == '#') {
                    char buf[8];
                    const gunichar ch = static_cast<gunichar>(e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16) : std::strtoul(e.c_str() + 1, nullptr, 10));
                    r.assign(buf, static_cast<size_t>(g_unichar_to_utf8(ch, buf)));
                }
                if (!r.empty()) {
                    dec += r;
                    i = semi;
                    continue;
                }
            }
        }
        dec += out[i];
    }
    std::string squeezed;
    int blanks = 0;
    std::istringstream in(dec);
    for (std::string l; std::getline(in, l);) {
        l = trim(l);
        if (l.empty()) {
            if (++blanks > 1) continue;
        } else blanks = 0;
        squeezed += l + "\n";
    }
    return trim(squeezed);
}

struct Attachment {
    std::string name, data;
};
struct Message {
    std::string from, to, cc, subject, date, messageId, references, text;
    std::vector<Attachment> attachments;
};

void walk(const std::string& raw, Message& m, std::string& html) {
    const size_t sep = raw.find("\r\n\r\n") != std::string::npos ? raw.find("\r\n\r\n") : raw.find("\n\n");
    const std::string headBlock = sep == std::string::npos ? raw : raw.substr(0, sep);
    std::string body = sep == std::string::npos ? "" : raw.substr(sep + (raw.compare(sep, 4, "\r\n\r\n") == 0 ? 4 : 2));
    const auto h = parseHeaders(headBlock);
    const std::string type = h.contains("content-type") ? h.at("content-type") : "text/plain";
    const std::string ltype = lowerOf(type);
    const std::string enc = h.contains("content-transfer-encoding") ? lowerOf(h.at("content-transfer-encoding")) : "";
    if (ltype.starts_with("multipart/")) {
        const std::string boundary = param(type, "boundary");
        if (boundary.empty()) return;
        const std::string marker = "--" + boundary;
        size_t at = body.find(marker);
        while (at != std::string::npos) {
            size_t start = body.find('\n', at);
            if (start == std::string::npos) break;
            ++start;
            const size_t next = body.find(marker, start);
            if (next == std::string::npos) break;
            walk(body.substr(start, next - start), m, html);
            if (body.compare(next + marker.size(), 2, "--") == 0) break;
            at = next;
        }
        return;
    }
    if (ltype.starts_with("message/rfc822")) return walk(body, m, html);
    std::string data = enc == "base64" ? decodeB64(body) : enc == "quoted-printable" ? decodeQP(body) : body;
    const std::string disposition = h.contains("content-disposition") ? h.at("content-disposition") : "";
    std::string name = param(disposition, "filename");
    if (name.empty()) name = param(type, "name");
    if (!name.empty() || lowerOf(disposition).starts_with("attachment")) {
        m.attachments.push_back({name.empty() ? "attachment" : name, data});
        return;
    }
    const std::string charset = param(type, "charset");
    if (ltype.starts_with("text/plain") && m.text.empty()) m.text = toUtf8(data, charset);
    else if (ltype.starts_with("text/html") && html.empty()) html = toUtf8(data, charset);
}

Message parseMessage(const std::string& raw) {
    Message m;
    const size_t sep = raw.find("\r\n\r\n") != std::string::npos ? raw.find("\r\n\r\n") : raw.find("\n\n");
    const auto h = parseHeaders(sep == std::string::npos ? raw : raw.substr(0, sep));
    auto get = [&](const char* k) { return h.contains(k) ? decodeWords(h.at(k)) : std::string(); };
    m.from = get("from"), m.to = get("to"), m.cc = get("cc"), m.subject = get("subject"), m.date = get("date");
    m.messageId = h.contains("message-id") ? h.at("message-id") : "";
    m.references = h.contains("references") ? h.at("references") : "";
    std::string html;
    walk(raw, m, html);
    if (trim(m.text).empty() && !html.empty()) m.text = htmlToText(html);
    std::string t;
    for (char c : m.text)
        if (c != '\r') t += c;
    m.text = trim(t);
    return m;
}

// The name in "Name <address>", or the address
std::string nameOf(const std::string& from) {
    const size_t lt = from.find('<');
    std::string n = trim(lt == std::string::npos ? from : from.substr(0, lt));
    if (n.size() >= 2 && n.front() == '"' && n.back() == '"') n = n.substr(1, n.size() - 2);
    return n.empty() ? trim(lt == std::string::npos ? from : from.substr(lt + 1, from.find('>') - lt - 1)) : n;
}
std::string addressOf(const std::string& from) {
    const size_t lt = from.find('<'), gt = from.find('>');
    return trim(lt != std::string::npos && gt != std::string::npos ? from.substr(lt + 1, gt - lt - 1) : from);
}
std::time_t parseDate(const std::string& d) {
    std::tm tm{};
    const std::string s = d.find(',') != std::string::npos ? d.substr(d.find(',') + 1) : d;
    const char* rest = strptime(s.c_str(), " %d %b %Y %H:%M:%S", &tm);
    if (!rest) return 0;
    std::time_t t = timegm(&tm);
    // The zone: +hhmm
    std::string z = trim(rest);
    if (z.size() >= 5 && (z[0] == '+' || z[0] == '-')) {
        const int off = (std::stoi(z.substr(1, 2)) * 60 + std::stoi(z.substr(3, 2))) * 60;
        t += z[0] == '+' ? -off : off;
    }
    return t;
}
std::string shortDate(std::time_t t) {
    if (t <= 0) return "";
    const std::time_t now = std::time(nullptr);
    char buf[32];
    std::tm lt = *std::localtime(&t), ln = *std::localtime(&now);
    if (lt.tm_year == ln.tm_year && lt.tm_yday == ln.tm_yday) std::strftime(buf, sizeof buf, "%H:%M", &lt);
    else if (lt.tm_year == ln.tm_year) std::strftime(buf, sizeof buf, "%-d %b", &lt);
    else std::strftime(buf, sizeof buf, "%-d %b %Y", &lt);
    return buf;
}

// ── The account

struct Account {
    std::string name, email, imap, smtp, imapSec = "ssl", smtpSec = "ssl";
    int imapPort = 993, smtpPort = 465;
    bool valid() const { return !email.empty() && !imap.empty() && !smtp.empty(); }
};
Account loadAccount() {
    Account a;
    const auto j = Config::get().value<json>("/mail/account", json::object());
    if (!j.is_object()) return a;
    a.name = j.value("name", ""), a.email = j.value("email", ""), a.imap = j.value("imap", ""), a.smtp = j.value("smtp", "");
    a.imapSec = j.value("imapSecurity", "ssl"), a.smtpSec = j.value("smtpSecurity", "ssl");
    a.imapPort = j.value("imapPort", 993), a.smtpPort = j.value("smtpPort", 465);
    return a;
}
void saveAccount(const Account& a) {
    Config::get().set("/mail/account", json{{"name", a.name}, {"email", a.email}, {"imap", a.imap}, {"smtp", a.smtp}, {"imapSecurity", a.imapSec},
                                            {"smtpSecurity", a.smtpSec}, {"imapPort", a.imapPort}, {"smtpPort", a.smtpPort}});
}
// The usual servers for an address's provider
Account guess(const std::string& email) {
    Account a;
    a.email = email;
    const std::string domain = lowerOf(email.substr(email.find('@') + 1));
    struct P {
        std::vector<std::string> domains;
        const char *imap, *smtp;
        int smtpPort;
        const char* smtpSec;
    };
    static const P providers[] = {
        {{"gmail.com", "googlemail.com"}, "imap.gmail.com", "smtp.gmail.com", 465, "ssl"},
        {{"outlook.com", "hotmail.com", "live.com", "msn.com"}, "outlook.office365.com", "smtp-mail.outlook.com", 587, "starttls"},
        {{"yahoo.com", "ymail.com"}, "imap.mail.yahoo.com", "smtp.mail.yahoo.com", 465, "ssl"},
        {{"icloud.com", "me.com", "mac.com"}, "imap.mail.me.com", "smtp.mail.me.com", 587, "starttls"},
        {{"fastmail.com", "fastmail.fm"}, "imap.fastmail.com", "smtp.fastmail.com", 465, "ssl"},
        {{"proton.me", "protonmail.com"}, "127.0.0.1", "127.0.0.1", 1025, "starttls"},
    };
    for (const auto& p : providers)
        if (std::ranges::find(p.domains, domain) != p.domains.end()) {
            a.imap = p.imap, a.smtp = p.smtp, a.smtpPort = p.smtpPort, a.smtpSec = p.smtpSec;
            if (domain.starts_with("proton")) a.imapPort = 1143, a.imapSec = "starttls"; // Proton Mail Bridge
            return a;
        }
    a.imap = "imap." + domain, a.smtp = "smtp." + domain;
    return a;
}
std::string imapUrl(const Account& a, const std::string& folder = {}) {
    std::string url = std::string(a.imapSec == "ssl" ? "imaps://" : "imap://") + a.imap + ":" + std::to_string(a.imapPort) + "/";
    if (!folder.empty()) {
        char* e = g_uri_escape_string(folder.c_str(), "/", false);
        url += e;
        g_free(e);
    }
    return url;
}

struct Summary {
    long uid = 0;
    std::string from, subject;
    std::time_t when = 0;
    bool seen = false;
};

struct State {
    Account account;
    std::string password; // in memory while open, from the keyring
    std::vector<std::string> folders;
    std::string folder = "INBOX";
    std::vector<Summary> list;
    long openUid = 0;
    Message open;
    bool loading = false, reading = false, composing = false, setup = false, sending = false;
    std::string error, status, armed;
    // Writing
    std::string to, cc, subject, body, inReplyTo, references;
    Flex *rail = nullptr, *middle = nullptr, *right = nullptr;
    Node* middleBox = nullptr; // the list's column: away while setting up
    EventLoop::TimerId checkTimer = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

void drawAll(const std::shared_ptr<State>& st);

// curl with the account's password in a file only you can read
void curl(const std::shared_ptr<State>& st, std::vector<std::string> args, std::function<void(int, const std::string&)> done, bool verbose = false,
          const std::string& input = {}) {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    std::string dir = rt && *rt ? rt : "/tmp";
    gchar* id = g_uuid_string_random();
    const std::string cfg = dir + "/material3ui-mail-" + id + ".conf";
    g_free(id);
    {
        const int fd = ::open(cfg.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) return done(-1, "");
        std::string user = st->account.email + ":" + st->password, q;
        for (char c : user) q += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
        const std::string line = "user = \"" + q + "\"\n";
        [[maybe_unused]] auto w = ::write(fd, line.data(), line.size());
        ::close(fd);
    }
    std::vector<std::string> cmd;
    if (verbose) {
        // FETCH answers reach only curl's trace (stderr): that's what's read
        cmd = {"sh", "-c", "exec curl -sv --max-time 60 -K \"$0\" \"$@\" 2>&1 >/dev/null", cfg};
    } else {
        cmd = {"curl", "-s", "--max-time", "120", "-K", cfg};
    }
    if (st->account.imapSec == "starttls" || st->account.smtpSec == "starttls") cmd.push_back("--ssl-reqd");
    cmd.insert(cmd.end(), args.begin(), args.end());
    m3::run(cmd, [cfg, done](int code, const std::string& out) {
        std::error_code ec;
        fs::remove(cfg, ec);
        done(code, out);
    }, input);
}

void loadFolders(const std::shared_ptr<State>& st) {
    curl(st, {imapUrl(st->account)}, [st](int code, const std::string& out) {
        if (!*st->alive || code != 0) return;
        st->folders.clear();
        std::istringstream in(out);
        static const std::regex list(R"re(\* LIST \(([^)]*)\) (?:"[^"]*"|NIL) (?:"((?:[^"\\]|\\.)*)"|(\S+)))re");
        std::smatch m;
        for (std::string l; std::getline(in, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!std::regex_search(l, m, list) || lowerOf(m[1].str()).find("noselect") != std::string::npos) continue;
            st->folders.push_back(m[2].matched ? m[2].str() : m[3].str());
        }
        // The inbox first
        std::ranges::stable_sort(st->folders, [](const std::string& a, const std::string& b) { return lowerOf(a) == "inbox" && lowerOf(b) != "inbox"; });
        drawAll(st);
    });
}

void loadList(const std::shared_ptr<State>& st, bool quiet = false) {
    st->loading = !quiet;
    if (!quiet) drawAll(st);
    const std::string folder = st->folder;
    curl(st, {imapUrl(st->account, folder), "-X", "UID SEARCH ALL"}, [st, folder, quiet](int code, const std::string& out) {
        if (!*st->alive || st->folder != folder) return;
        if (code != 0) {
            st->loading = false;
            st->error = code == 67 ? "The server didn't take the password (an app password may be needed)" : std::format("Couldn't reach the mail server (curl {})", code);
            return drawAll(st);
        }
        std::vector<long> uids;
        if (const size_t at = out.find("* SEARCH"); at != std::string::npos) {
            std::istringstream in(out.substr(at + 8));
            for (long u; in >> u;) uids.push_back(u);
        }
        if (uids.empty()) {
            st->list.clear();
            st->loading = false;
            return drawAll(st);
        }
        // The latest 80, by sequence number (curl shows FETCH answers only in its trace)
        const size_t n = uids.size(), from = n > 80 ? n - 79 : 1;
        curl(st, {imapUrl(st->account, folder), "-X", std::format("FETCH {}:{} (UID FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])", from, n)},
             [st, folder, quiet](int, const std::string& trace) {
                 if (!*st->alive || st->folder != folder) return;
                 std::vector<Summary> out;
                 std::istringstream in(trace);
                 static const std::regex fetchLine(R"(^\* \d+ FETCH \(.*UID (\d+).*FLAGS \(([^)]*)\))");
                 Summary cur;
                 bool inBlock = false;
                 std::string head;
                 for (std::string l; std::getline(in, l);) {
                     if (!l.starts_with("< ")) continue;
                     l = l.substr(2);
                     if (!l.empty() && l.back() == '\r') l.pop_back();
                     std::smatch m;
                     if (std::regex_search(l, m, fetchLine)) {
                         cur = Summary{};
                         cur.uid = std::stol(m[1].str());
                         cur.seen = m[2].str().find("\\Seen") != std::string::npos;
                         inBlock = true;
                         head.clear();
                         continue;
                     }
                     if (!inBlock) continue;
                     if (l == ")") {
                         const auto h = parseHeaders(head);
                         cur.from = h.contains("from") ? nameOf(decodeWords(h.at("from"))) : "";
                         cur.subject = h.contains("subject") ? decodeWords(h.at("subject")) : "";
                         cur.when = h.contains("date") ? parseDate(h.at("date")) : 0;
                         out.push_back(cur);
                         inBlock = false;
                         continue;
                     }
                     head += l + "\n";
                 }
                 std::ranges::sort(out, [](const Summary& a, const Summary& b) { return a.uid > b.uid; });
                 // New in the inbox since last time: said
                 if (folder == "INBOX" && !out.empty()) {
                     const long seen = Config::get().value<long>("/mail/lastUid", 0);
                     size_t fresh = 0;
                     for (const auto& s : out)
                         if (s.uid > seen && !s.seen) ++fresh;
                     if (seen > 0 && fresh > 0 && quiet)
                         m3::run({"notify-send", "-a", "Mail", "-i", "mail-unread", fresh == 1 ? "New email" : std::format("{} new emails", fresh),
                                  fresh == 1 ? out.front().from + ": " + out.front().subject : ""}, nullptr);
                     Config::get().set("/mail/lastUid", out.front().uid);
                 }
                 st->list = std::move(out);
                 st->loading = false;
                 st->error.clear();
                 drawAll(st);
             }, true);
    });
}

void openMessage(const std::shared_ptr<State>& st, long uid) {
    st->openUid = uid;
    st->reading = true;
    st->composing = false;
    st->armed.clear();
    st->open = Message{};
    drawAll(st);
    const std::string folder = st->folder;
    curl(st, {imapUrl(st->account, folder) + ";UID=" + std::to_string(uid)}, [st, uid](int code, const std::string& raw) {
        if (!*st->alive || st->openUid != uid) return;
        st->reading = false;
        if (code != 0) st->open.text = "Couldn't fetch this message";
        else st->open = parseMessage(raw);
        for (auto& s : st->list)
            if (s.uid == uid) s.seen = true; // fetching it marked it read
        drawAll(st);
    });
}

void imapCommand(const std::shared_ptr<State>& st, const std::string& command, std::function<void(bool)> done = {}) {
    curl(st, {imapUrl(st->account, st->folder), "-X", command}, [done](int code, const std::string&) {
        if (done) done(code == 0);
    });
}

void deleteMessage(const std::shared_ptr<State>& st, long uid) {
    // To the trash when there is one (MOVE), else flagged and expunged
    std::string trash;
    for (const auto& f : st->folders)
        if (lowerOf(f) == "trash" || lowerOf(f).ends_with("/trash") || lowerOf(f) == "deleted items" || lowerOf(f).ends_with("/bin")) trash = f;
    auto after = [st, uid](bool) {
        if (!*st->alive) return;
        std::erase_if(st->list, [uid](const Summary& s) { return s.uid == uid; });
        if (st->openUid == uid) st->openUid = 0;
        drawAll(st);
    };
    if (!trash.empty() && trash != st->folder) imapCommand(st, std::format("UID MOVE {} \"{}\"", uid, trash), after);
    else
        imapCommand(st, std::format("UID STORE {} +FLAGS (\\Deleted)", uid), [st, after](bool ok) {
            if (ok) imapCommand(st, "EXPUNGE", after);
        });
}

std::string quoteBody(const Message& m) {
    std::string q = "\n\nOn " + m.date + ", " + nameOf(m.from) + " wrote:\n";
    std::istringstream in(m.text);
    for (std::string l; std::getline(in, l);) q += "> " + l + "\n";
    return q;
}

void startCompose(const std::shared_ptr<State>& st, const std::string& mode) {
    st->composing = true;
    st->to = st->cc = st->subject = st->body = st->inReplyTo = st->references = "";
    st->status.clear();
    const Message& m = st->open;
    if (mode == "reply" || mode == "all") {
        st->to = addressOf(m.from);
        if (mode == "all") {
            std::string others;
            for (const std::string* list : {&m.to, &m.cc}) {
                std::istringstream in(*list);
                for (std::string a; std::getline(in, a, ',');) {
                    const std::string addr = addressOf(a);
                    if (!addr.empty() && lowerOf(addr) != lowerOf(st->account.email) && lowerOf(addr) != lowerOf(st->to)) others += (others.empty() ? "" : ", ") + addr;
                }
            }
            st->cc = others;
        }
        st->subject = lowerOf(m.subject).starts_with("re:") ? m.subject : "Re: " + m.subject;
        st->body = quoteBody(m);
        st->inReplyTo = m.messageId;
        st->references = trim(m.references + " " + m.messageId);
    } else if (mode == "forward") {
        st->subject = lowerOf(m.subject).starts_with("fwd:") ? m.subject : "Fwd: " + m.subject;
        st->body = "\n\n---------- Forwarded message ----------\nFrom: " + m.from + "\nDate: " + m.date + "\nSubject: " + m.subject + "\nTo: " + m.to + "\n\n" + m.text;
    }
    drawAll(st);
}

// A header's words, encoded when they're not plain ASCII
std::string encodeHeader(const std::string& s) {
    if (std::ranges::all_of(s, [](char c) { return static_cast<unsigned char>(c) < 0x80; })) return s;
    gchar* b = g_base64_encode(reinterpret_cast<const guchar*>(s.data()), s.size());
    std::string out = std::string("=?UTF-8?B?") + b + "?=";
    g_free(b);
    return out;
}

void send(const std::shared_ptr<State>& st) {
    std::vector<std::string> rcpts;
    for (const std::string* list : {&st->to, &st->cc}) {
        std::istringstream in(*list);
        for (std::string a; std::getline(in, a, ',');)
            if (const std::string addr = addressOf(a); addr.find('@') != std::string::npos) rcpts.push_back(addr);
    }
    if (rcpts.empty()) {
        st->status = "Who to? Put an address in To";
        return drawAll(st);
    }
    char date[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S %z", std::localtime(&now));
    gchar* id = g_uuid_string_random();
    const std::string domain = st->account.email.substr(st->account.email.find('@') + 1);
    std::string msg = "From: " + (st->account.name.empty() ? "" : encodeHeader(st->account.name) + " ") + "<" + st->account.email + ">\r\n";
    msg += "To: " + st->to + "\r\n";
    if (!trim(st->cc).empty()) msg += "Cc: " + st->cc + "\r\n";
    msg += "Subject: " + encodeHeader(st->subject) + "\r\nDate: " + date + "\r\nMessage-ID: <" + id + "@" + domain + ">\r\n";
    g_free(id);
    if (!st->inReplyTo.empty()) msg += "In-Reply-To: " + st->inReplyTo + "\r\nReferences: " + st->references + "\r\n";
    msg += "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 8bit\r\nUser-Agent: Material3UI Mail\r\n\r\n";
    std::istringstream in(st->body);
    for (std::string l; std::getline(in, l);) msg += (l.starts_with(".") ? "." : "") + l + "\r\n";
    std::vector<std::string> args = {std::string(st->account.smtpSec == "ssl" ? "smtps://" : "smtp://") + st->account.smtp + ":" + std::to_string(st->account.smtpPort),
                                     "--mail-from", st->account.email, "-T", "-"};
    for (const auto& r : rcpts) args.insert(args.end() - 2, {"--mail-rcpt", r});
    st->sending = true;
    st->status = "Sending…";
    drawAll(st);
    curl(st, args, [st, msg](int code, const std::string&) {
        if (!*st->alive) return;
        st->sending = false;
        if (code != 0) {
            st->status = std::format("It didn't send (curl {}): check the address and the server in Settings", code);
            return drawAll(st);
        }
        // A copy in Sent, when there is one (APPEND)
        std::string sent;
        for (const auto& f : st->folders)
            if (lowerOf(f) == "sent" || lowerOf(f).ends_with("/sent") || lowerOf(f) == "sent items" || lowerOf(f) == "sent mail" || lowerOf(f).ends_with("/sent mail")) sent = f;
        if (!sent.empty()) {
            // From a file: an IMAP upload needs its size before it starts
            const char* rt = std::getenv("XDG_RUNTIME_DIR");
            gchar* id2 = g_uuid_string_random();
            const std::string file = std::string(rt && *rt ? rt : "/tmp") + "/material3ui-mail-" + id2 + ".eml";
            g_free(id2);
            const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd >= 0) {
                [[maybe_unused]] auto w = ::write(fd, msg.data(), msg.size());
                ::close(fd);
                curl(st, {imapUrl(st->account, sent), "-T", file}, [file](int, const std::string&) {
                    std::error_code ec;
                    fs::remove(file, ec);
                });
            }
        }
        st->composing = false;
        st->status.clear();
        m3::run({"notify-send", "-a", "Mail", "-i", "mail-send", "Sent", st->subject}, nullptr);
        drawAll(st);
    }, false, msg);
}

// ── Drawing

void drawRail(const std::shared_ptr<State>& st) {
    auto* r = st->rail;
    r->clearChildren();
    auto* brand = r->add<Label>("Mail");
    brand->setSize(30)->setWeight(400);
    brand->alignSelf = Align::Start;
    caption(r, st->account.email)->setMaxWidth(190);
    auto* write = r->add<Button>(Flex::Direction::Row);
    write->setPadding({18, 14})->setSpacing(10)->setRadius(Radii(18))->setBackground("primary_container", false);
    write->add<Icon>("edit", 22)->setColor("on_primary_container", false);
    write->add<Label>("Write")->setSize(15)->setWeight(600)->setColor("on_primary_container", false);
    write->alignSelf = Align::Start;
    write->onClicked = [st](uint32_t b) {
        if (b == BTN_LEFT) startCompose(st, "new");
    };
    r->add<Node>()->fixedHeight = 6;
    auto iconFor = [](const std::string& f) {
        const std::string l = lowerOf(f);
        if (l == "inbox") return "inbox";
        if (l.find("sent") != std::string::npos) return "send";
        if (l.find("draft") != std::string::npos) return "draft";
        if (l.find("trash") != std::string::npos || l.find("deleted") != std::string::npos || l.ends_with("bin")) return "delete";
        if (l.find("spam") != std::string::npos || l.find("junk") != std::string::npos) return "report";
        if (l.find("archive") != std::string::npos || l.find("all mail") != std::string::npos) return "archive";
        return "folder";
    };
    for (const auto& f : st->folders.empty() ? std::vector<std::string>{"INBOX"} : st->folders) {
        const bool here = f == st->folder;
        auto* b = r->add<Button>(Flex::Direction::Row);
        pill(b)->setPadding({16, 9})->setSpacing(12);
        if (here) b->setBackground("secondary_container", false);
        b->add<Icon>(iconFor(f), 20)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
        const std::string shown = lowerOf(f) == "inbox" ? "Inbox" : f.substr(f.rfind('/') == std::string::npos ? 0 : f.rfind('/') + 1);
        b->add<Label>(shown)->setSize(14)->setWeight(here ? 600 : 500)->setMaxWidth(140)->setColor(here ? "on_secondary_container" : "on_surface", false);
        b->onClicked = [st, f](uint32_t btn) {
            if (btn != BTN_LEFT) return;
            st->folder = f;
            st->openUid = 0;
            st->list.clear();
            loadList(st);
        };
    }
}

void drawMiddle(const std::shared_ptr<State>& st) {
    auto* m = st->middle;
    m->clearChildren();
    auto* head = m->add<Flex>(Flex::Direction::Row);
    head->setSpacing(6)->setAlignItems(Align::Center)->setPadding({4, 0});
    auto* t = head->add<Label>(lowerOf(st->folder) == "inbox" ? "Inbox" : st->folder);
    t->setSize(20)->setWeight(500)->setMaxWidth(240);
    t->grow = true;
    iconButton(head, "refresh", [st] { loadList(st); }, 20)->tooltip = "Check for new mail";
    if (!st->error.empty()) {
        caption(m, st->error)->setColor("error", false)->setMaxLines(3)->setMaxWidth(300);
        pillButton(m, "Account settings", false, [st] {
            st->setup = true;
            drawAll(st);
        }, "settings");
    }
    if (st->loading && st->list.empty()) caption(m, "Loading…");
    else if (!st->loading && st->list.empty() && st->error.empty()) caption(m, "Nothing here");
    for (const auto& s : st->list) {
        const bool here = s.uid == st->openUid;
        auto* row = m->add<Button>(Flex::Direction::Column);
        row->setPadding({14, 10})->setSpacing(2)->setRadius(Radii(16))->setAlignItems(Align::Stretch);
        if (here) row->setBackground("secondary_container", false);
        auto* top = row->add<Flex>(Flex::Direction::Row);
        top->setSpacing(8)->setAlignItems(Align::Center);
        if (!s.seen) top->add<Icon>("circle", 9)->setFill(true)->setColor("primary", false);
        auto* from = top->add<Label>(s.from.empty() ? "(unknown)" : s.from);
        from->setSize(14)->setWeight(s.seen ? 400 : 700)->setMaxWidth(200);
        from->grow = true;
        from->givesWay = true;
        top->add<Label>(shortDate(s.when))->setSize(12)->setColor(s.seen ? "on_surface_variant" : "primary", false);
        row->add<Label>(s.subject.empty() ? "(no subject)" : s.subject)->setSize(13)->setWeight(s.seen ? 400 : 600)->setMaxWidth(300)->setColor(s.seen ? "on_surface_variant" : "on_surface", false);
        row->onClicked = [st, uid = s.uid](uint32_t b) {
            if (b == BTN_LEFT) openMessage(st, uid);
        };
    }
}

TextField* labelled(Flex* form, const std::string& label, std::string& value, const std::string& placeholder = {}) {
    auto* r = form->add<Flex>(Flex::Direction::Row);
    r->setSpacing(12)->setAlignItems(Align::Center);
    auto* l = r->add<Label>(label);
    l->setSize(13)->setColor("on_surface_variant", false);
    l->fixedWidth = 64;
    auto* f = r->add<TextField>();
    f->grow = true;
    f->setText(value);
    f->setPlaceholder(placeholder);
    f->onChanged = [&value](const std::string& t) { value = t; };
    return f;
}

void drawCompose(const std::shared_ptr<State>& st, Flex* r) {
    r->add<Label>("New message")->setSize(22)->setWeight(500);
    auto* form = r->add<Flex>(Flex::Direction::Column);
    form->setSpacing(8)->setAlignItems(Align::Stretch)->setPadding({16, 12})->setRadius(Radii(20))->setBackground("surface_container_high", false);
    auto* to = labelled(form, "To", st->to, "name@example.com, another@example.com");
    labelled(form, "Cc", st->cc);
    labelled(form, "Subject", st->subject);
    auto* body = form->add<TextArea>();
    body->setMinLines(14);
    body->setText(st->body);
    body->setPlaceholder("Your message");
    body->onChanged = [st](const std::string& t) { st->body = t; };
    if (st->to.empty())
        EventLoop::get().defer([to, alive = st->alive] {
            if (*alive) to->focus();
        });
    if (!st->status.empty()) caption(r, st->status);
    auto* btns = r->add<Flex>(Flex::Direction::Row);
    btns->setSpacing(8);
    pillButton(btns, st->sending ? "Sending…" : "Send", true, [st] {
        if (!st->sending) send(st);
    }, "send");
    pillButton(btns, "Discard", false, [st] {
        st->composing = false;
        drawAll(st);
    }, "delete");
}

void drawReader(const std::shared_ptr<State>& st) {
    auto* r = st->right;
    r->clearChildren();
    if (st->composing) return drawCompose(st, r);
    if (st->openUid == 0) {
        auto* e = r->add<Flex>(Flex::Direction::Column);
        e->setSpacing(10)->setPadding({0, 100})->setAlignItems(Align::Center);
        e->add<Icon>("mail", 56)->setColor("primary", false);
        e->add<Label>("Pick a message")->setSize(18)->setWeight(600);
        return;
    }
    if (st->reading) return caption(r, "Opening…"), void();
    const Message& m = st->open;
    r->add<Label>(m.subject.empty() ? "(no subject)" : m.subject)->setSize(22)->setWeight(600)->setMaxWidth(560)->setMaxLines(3);
    auto* who = r->add<Flex>(Flex::Direction::Row);
    who->setSpacing(12)->setAlignItems(Align::Center);
    auto* av = who->add<Flex>(Flex::Direction::Row);
    av->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(20))->setBackground("tertiary_container", false);
    av->fixedWidth = av->fixedHeight = 40;
    const std::string n = nameOf(m.from);
    char buf[8];
    av->add<Label>(n.empty() ? "?" : std::string(buf, static_cast<size_t>(g_unichar_to_utf8(g_unichar_toupper(g_utf8_get_char(n.c_str())), buf))))
        ->setSize(17)
        ->setWeight(600)
        ->setColor("on_tertiary_container", false);
    auto* t = who->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(1);
    t->grow = true;
    t->givesWay = true;
    t->add<Label>(n + "  <" + addressOf(m.from) + ">")->setSize(14)->setWeight(500)->setMaxWidth(460);
    t->add<Label>("To " + m.to + (m.cc.empty() ? "" : "  ·  Cc " + m.cc))->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(460);
    who->add<Label>(shortDate(parseDate(m.date)))->setSize(12)->setColor("on_surface_variant", false);
    auto* actions = r->add<Flow>();
    actions->spacing = 8;
    pillButton(actions, "Reply", true, [st] { startCompose(st, "reply"); }, "reply");
    pillButton(actions, "Reply all", false, [st] { startCompose(st, "all"); }, "reply_all");
    pillButton(actions, "Forward", false, [st] { startCompose(st, "forward"); }, "forward");
    pillButton(actions, "Mark unread", false, [st, uid = st->openUid] {
        imapCommand(st, std::format("UID STORE {} -FLAGS (\\Seen)", uid));
        for (auto& s : st->list)
            if (s.uid == uid) s.seen = false;
        st->openUid = 0;
        drawAll(st);
    }, "mark_email_unread");
    const bool armed = st->armed == std::to_string(st->openUid);
    auto* del = pillButton(actions, armed ? "Tap again to delete" : "Delete", false, [st, uid = st->openUid] {
        if (st->armed != std::to_string(uid)) {
            st->armed = std::to_string(uid);
            return drawAll(st);
        }
        st->armed.clear();
        deleteMessage(st, uid);
    }, "delete");
    if (armed) del->setBackground("error_container", false);
    if (!m.attachments.empty()) {
        auto* at = r->add<Flow>();
        at->spacing = 8;
        for (const auto& a : m.attachments) {
            auto* b = pillButton(at, std::format("{}  ·  {}", a.name, fileops::humanSize(a.data.size())), false, [a] {
                const char* dl = std::getenv("XDG_DOWNLOAD_DIR");
                const std::string dir = dl && *dl ? dl : paths::home() + "/Downloads";
                std::error_code ec;
                fs::create_directories(dir, ec);
                const std::string name = fileops::freeName(dir, fs::path(a.name).filename().string());
                std::ofstream(dir + "/" + name, std::ios::binary).write(a.data.data(), static_cast<std::streamsize>(a.data.size()));
                m3::run({"notify-send", "-a", "Mail", "-i", "document-save", "Saved to Downloads", name}, nullptr);
            }, "attach_file");
            b->tooltip = "Save to Downloads";
        }
    }
    auto* bodyBox = r->add<Flex>(Flex::Direction::Column);
    bodyBox->setPadding({18, 16})->setRadius(Radii(20))->setBackground("surface_container_lowest", false)->setAlignItems(Align::Start);
    // The message, quoted lines dimmed
    std::istringstream in(m.text.empty() ? "(This message has no text)" : m.text);
    std::string para;
    bool quoted = false;
    auto flush = [&] {
        if (para.empty()) return;
        auto* l = bodyBox->add<Label>(para);
        l->setSize(14)->setMaxWidth(580)->setMaxLines(kAllLines)->setColor(quoted ? "on_surface_variant" : "on_surface", false);
        para.clear();
    };
    for (std::string line; std::getline(in, line);) {
        const bool q = line.starts_with(">");
        if (q != quoted) flush(), quoted = q;
        para += (para.empty() ? "" : "\n") + line;
        if (para.size() > 3000) flush();
    }
    flush();
}

void drawSetup(const std::shared_ptr<State>& st) {
    auto* r = st->right;
    st->rail->clearChildren();
    st->middle->clearChildren();
    auto* brand = st->rail->add<Label>("Mail");
    brand->setSize(30)->setWeight(400);
    r->clearChildren();
    auto draft = std::make_shared<Account>(st->account);
    auto pw = std::make_shared<std::string>();
    r->add<Label>(st->account.valid() ? "Your account" : "Add your email account")->setSize(24)->setWeight(500);
    caption(r, "Your password stays in your keyring. Gmail, Yahoo, iCloud and Outlook want an app password (made in your account's security settings), not your usual one.")
        ->setMaxWidth(560)
        ->setMaxLines(3);
    auto* form = r->add<Flex>(Flex::Direction::Column);
    form->setSpacing(8)->setAlignItems(Align::Stretch)->setPadding({16, 12})->setRadius(Radii(20))->setBackground("surface_container_high", false);
    labelled(form, "Name", draft->name, "Your name, as people see it");
    auto* email = labelled(form, "Email", draft->email, "you@example.com");
    auto* pass = labelled(form, "Password", *pw, st->account.valid() ? "Leave empty to keep the saved one" : "The (app) password");
    pass->setSecret(true);
    auto* adv = form->add<Flex>(Flex::Direction::Column);
    adv->setSpacing(8)->setAlignItems(Align::Stretch);
    auto servers = [adv, draft] {
        adv->clearChildren();
        caption(adv, "Servers (filled in for most providers)");
        auto port = std::make_shared<std::string>(std::to_string(draft->imapPort)), sport = std::make_shared<std::string>(std::to_string(draft->smtpPort));
        labelled(adv, "IMAP", draft->imap, "imap.example.com");
        auto* ip = labelled(adv, "Port", *port);
        ip->onChanged = [draft](const std::string& t) { draft->imapPort = std::atoi(t.c_str()); };
        auto r1 = adv->add<Flex>(Flex::Direction::Row);
        r1->setSpacing(8);
        connectedGroup<std::string>(r1, draft->imapSec, {{"ssl", "SSL/TLS"}, {"starttls", "STARTTLS"}, {"none", "None"}}, [draft](std::string v) { draft->imapSec = v; });
        labelled(adv, "SMTP", draft->smtp, "smtp.example.com");
        auto* sp = labelled(adv, "Port", *sport);
        sp->onChanged = [draft](const std::string& t) { draft->smtpPort = std::atoi(t.c_str()); };
        auto r2 = adv->add<Flex>(Flex::Direction::Row);
        r2->setSpacing(8);
        connectedGroup<std::string>(r2, draft->smtpSec, {{"ssl", "SSL/TLS"}, {"starttls", "STARTTLS"}, {"none", "None"}}, [draft](std::string v) { draft->smtpSec = v; });
    };
    servers();
    email->onChanged = [draft, servers, adv](const std::string& t) {
        draft->email = t;
        if (t.find('@') == std::string::npos || t.back() == '@') return;
        const Account g = guess(t);
        draft->imap = g.imap, draft->smtp = g.smtp, draft->imapPort = g.imapPort, draft->smtpPort = g.smtpPort, draft->imapSec = g.imapSec, draft->smtpSec = g.smtpSec;
        servers();
    };
    if (!st->error.empty()) caption(r, st->error)->setColor("error", false)->setMaxWidth(560)->setMaxLines(3);
    auto* btns = r->add<Flex>(Flex::Direction::Row);
    btns->setSpacing(8);
    pillButton(btns, "Sign in", true, [st, draft, pw] {
        if (!draft->valid() || draft->email.find('@') == std::string::npos) {
            st->error = "An email address and its servers, please";
            return drawAll(st);
        }
        const std::string password = pw->empty() ? st->password : *pw;
        if (password.empty()) {
            st->error = "And the password";
            return drawAll(st);
        }
        st->account = *draft;
        st->password = password;
        st->error = "Signing in…";
        drawAll(st);
        // Tried before it's kept
        curl(st, {imapUrl(st->account)}, [st, password](int code, const std::string&) {
            if (!*st->alive) return;
            if (code != 0) {
                st->error = code == 67 ? "The server turned the password down (an app password may be needed)" : std::format("Couldn't reach the server (curl {}): check its name and port", code);
                return drawAll(st);
            }
            saveAccount(st->account);
            Secrets::get().store("Mail: " + st->account.email, {{"application", "material3ui-mail"}, {"account", st->account.email}}, password, [](bool) {});
            st->error.clear();
            st->setup = false;
            loadFolders(st);
            loadList(st);
        });
    }, "login");
    if (st->account.valid())
        pillButton(btns, "Cancel", false, [st] {
            st->setup = false;
            st->error.clear();
            drawAll(st);
        });
}

void drawAll(const std::shared_ptr<State>& st) {
    EventLoop::get().defer([st] {
        if (!*st->alive) return;
        const bool setting = st->setup || !st->account.valid();
        st->middleBox->setVisible(!setting);
        if (setting) return drawSetup(st);
        drawRail(st);
        drawMiddle(st);
        drawReader(st);
    });
}

class Keeper : public Node {
public:
    explicit Keeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~Keeper() override {
        *m_st->alive = false;
        if (m_st->checkTimer) EventLoop::get().cancel(m_st->checkTimer);
        m_st->password.assign(m_st->password.size(), '\0');
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

void mailApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<State>();
    st->account = loadAccount();
    card->setPadding(Insets(12))->setSpacing(8);
    card->add<Keeper>(st);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(12)->setAlignItems(Align::Stretch);
    columns->grow = true;
    st->rail = columns->add<Flex>(Flex::Direction::Column);
    st->rail->setSpacing(4)->setAlignItems(Align::Stretch)->setPadding({4, 8});
    st->rail->fixedWidth = 210;
    auto* midScroll = columns->add<ScrollView>();
    midScroll->maxHeight = 1e6f;
    midScroll->fixedWidth = 330;
    st->middleBox = midScroll;
    st->middle = midScroll->add<Flex>(Flex::Direction::Column);
    st->middle->setSpacing(2)->setAlignItems(Align::Stretch)->setPadding({0, 0, 8, 8});
    auto* rightScroll = columns->add<ScrollView>();
    rightScroll->maxHeight = 1e6f;
    rightScroll->grow = true;
    st->right = rightScroll->add<Flex>(Flex::Direction::Column);
    st->right->setSpacing(12)->setAlignItems(Align::Stretch)->setPadding({8, 8, 16, 16});
    popup.onKey = [st](const KeyEvent& e) {
        if (e.ctrl() && (e.baseSym == 'n' || e.baseSym == 'N')) return startCompose(st, "new"), true;
        if (e.ctrl() && (e.baseSym == 'r' || e.baseSym == 'R') && st->openUid && !st->composing) return startCompose(st, e.shift() ? "all" : "reply"), true;
        if (e.ctrl() && e.sym == XKB_KEY_Return && st->composing) return send(st), true;
        if (e.sym == XKB_KEY_F5) return loadList(st), true;
        if (e.sym == XKB_KEY_Escape && st->composing) {
            st->composing = false;
            drawAll(st);
            return true;
        }
        return false;
    };
    // mailto: links
    mailGo = [st](const std::string& a) {
        if (!*st->alive || !a.starts_with("mailto:")) return;
        startCompose(st, "new");
        std::string to = a.substr(7);
        const size_t q = to.find('?');
        std::string query = q == std::string::npos ? "" : to.substr(q + 1);
        to = to.substr(0, q);
        gchar* u = g_uri_unescape_string(to.c_str(), nullptr);
        st->to = u ? u : to;
        g_free(u);
        std::istringstream in(query);
        for (std::string kv; std::getline(in, kv, '&');) {
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            gchar* v = g_uri_unescape_string(kv.substr(eq + 1).c_str(), nullptr);
            const std::string k = lowerOf(kv.substr(0, eq)), val = v ? v : "";
            g_free(v);
            if (k == "subject") st->subject = val;
            else if (k == "body") st->body = val;
            else if (k == "cc") st->cc = val;
        }
        drawAll(st);
    };
    auto start = [st, arg] {
        drawAll(st);
        if (!st->account.valid()) return;
        loadFolders(st);
        loadList(st);
        // Every few minutes, for new mail
        const int every = Config::get().value<int>("/mail/checkEvery", 5);
        if (every > 0)
            st->checkTimer = EventLoop::get().addTimer(every * 60000, [st] {
                if (*st->alive && st->folder == "INBOX") loadList(st, true);
            }, true);
        if (!arg.empty()) mailGo(arg);
    };
    if (!st->account.valid()) return start();
    // The password, from the keyring
    Secrets::get().lookup({{"application", "material3ui-mail"}, {"account", st->account.email}}, [st, start](std::optional<std::string> p) {
        if (!*st->alive) return;
        if (!p || p->empty()) {
            st->setup = true;
            st->error = "Your password wasn't in the keyring: enter it again";
        } else {
            st->password = *p;
        }
        start();
    });
}

void mailClosed() {
    mailGo = nullptr;
}

void mailSettings(Flex* page) {
    auto* g = group(page, "Checking");
    choiceRow<int>(g, "schedule", "Check for new mail", "While Mail is open, with a notification for new ones", "/mail/checkEvery", 5,
                   {{0, "Only when asked"}, {2, "Every 2 min"}, {5, "Every 5 min"}, {15, "Every 15 min"}});
    const Account a = loadAccount();
    auto* acc = group(page, "Account");
    row(acc, "account_circle", a.valid() ? a.email : "No account yet", a.valid() ? a.imap + "  ·  " + a.smtp : "Mail asks for it when it opens");
    if (a.valid())
        pillButton(acc, "Sign out", false, [] { Config::get().set("/mail/account", json::object()); }, "logout");
    caption(page, "  Your password is kept in your keyring and given to curl in a file only you can read. After signing out, Settings → Passwords removes it (\"Mail: …\").");
}

} // namespace m3::shell::panels
