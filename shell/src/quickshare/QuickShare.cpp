#include "quickshare/QuickShare.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "quickshare/Crypto.h"
#include "quickshare/Mdns.h"
#include "quickshare/Proto.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <random>
#include <set>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3::quickshare {

using proto::Bytes;
using proto::Reader;
using proto::Writer;

namespace {

// ── Wire constants (offline_wire_formats.proto, ukey.proto, wire_format.proto) ──
enum OfflineType { CONNECTION_REQUEST = 1, CONNECTION_RESPONSE = 2, PAYLOAD_TRANSFER = 3, KEEP_ALIVE = 5, DISCONNECTION = 6 };
enum Ukey2Type { ALERT = 1, CLIENT_INIT = 2, SERVER_INIT = 3, CLIENT_FINISH = 4 };
enum ShareType { INTRODUCTION = 1, RESPONSE = 2, PAIRED_KEY_ENCRYPTION = 3, PAIRED_KEY_RESULT = 4, CANCEL = 6 };
constexpr int kP256Sha512 = 100;
constexpr int kPayloadBytes = 1, kPayloadFile = 2;
constexpr int kLastChunk = 1;
constexpr const char* kServiceType = "_FC9F5ED42C8A._tcp";

std::string base64url(const Bytes& data) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t n = static_cast<uint8_t>(data[i]) << 16 | static_cast<uint8_t>(data[i + 1]) << 8 | static_cast<uint8_t>(data[i + 2]);
        out += abc[n >> 18 & 63];
        out += abc[n >> 12 & 63];
        out += abc[n >> 6 & 63];
        out += abc[n & 63];
    }
    if (i + 1 == data.size()) {
        const uint32_t n = static_cast<uint8_t>(data[i]) << 16;
        out += abc[n >> 18 & 63];
        out += abc[n >> 12 & 63];
    } else if (i + 2 == data.size()) {
        const uint32_t n = static_cast<uint8_t>(data[i]) << 16 | static_cast<uint8_t>(data[i + 1]) << 8;
        out += abc[n >> 18 & 63];
        out += abc[n >> 12 & 63];
        out += abc[n >> 6 & 63];
    }
    return out;
}

// A big-endian integer as exactly 32 bytes (Java's BigInteger adds a 0 for
// the sign, and may drop leading zeros)
Bytes fit32(Bytes b) {
    while (b.size() > 32 && b[0] == '\0') b.erase(0, 1);
    while (b.size() < 32) b.insert(b.begin(), '\0');
    return b;
}

// And back: a leading 0 when the top bit is set, so it reads as positive
Bytes signedBig(const Bytes& b) {
    return !b.empty() && (static_cast<uint8_t>(b[0]) & 0x80) ? std::string(1, '\0') + b : b;
}

// Chromium's four-digit PIN from the UKEY2 authentication string
std::string pinFrom(const Bytes& auth) {
    int hash = 0, multiplier = 1;
    for (char c : auth) {
        hash = (hash + static_cast<int8_t>(c) * multiplier) % 9973;
        multiplier = (multiplier * 31) % 9973;
    }
    return std::format("{:04}", std::abs(hash));
}

std::string downloads() {
    std::string dir = paths::home() + "/Downloads";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// name.ext, then "name (1).ext"… when taken; never outside the folder
std::string freePath(const std::string& name) {
    std::string base = fs::path(name).filename().string();
    if (base.empty() || base == "." || base == "..") base = "Received file";
    const fs::path dir = downloads();
    fs::path p = dir / base;
    std::error_code ec;
    for (int n = 1; fs::exists(p, ec); ++n) {
        const fs::path b(base);
        p = dir / std::format("{} ({}){}", b.stem().string(), n, b.extension().string());
    }
    return p.string();
}

} // namespace

// ── A connection from a phone: the handshake, then the transfer ──

class Connection : public EventSource {
public:
    Connection(int fd, QuickShare::Transfer& t) : m_fd(fd), m_id(t.id) { EventLoop::get().add(this); }
    ~Connection() override {
        EventLoop::get().remove(this);
        if (m_keepAlive) EventLoop::get().cancel(m_keepAlive);
        if (m_answerTimer) EventLoop::get().cancel(m_answerTimer);
        for (auto& [_, f] : m_files)
            if (f.out) std::fclose(f.out);
        if (m_fd >= 0) close(m_fd);
    }

    int fd() const override { return m_fd; }
    short events() const override { return static_cast<short>(POLLIN | (m_out.empty() ? 0 : POLLOUT)); }
    void dispatch(short revents) override {
        if (m_closed) return;
        if (revents & POLLOUT) flush();
        if (revents & (POLLIN | POLLHUP | POLLERR)) readSome();
    }

    void accept();
    void decline();
    int id() const { return m_id; }

private:
    enum class Step { Request, ClientInit, ClientFinish, Response, Secure };

    QuickShare::Transfer* t() { return const_cast<QuickShare::Transfer*>(QuickShare::get().transfer(m_id)); }

    void readSome() {
        char buf[65536];
        for (;;) {
            const ssize_t n = recv(m_fd, buf, sizeof buf, 0);
            if (n > 0) {
                m_in.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) return closeWith(m_doneOk ? "" : "The other device left");
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            return closeWith("Connection lost");
        }
        // Each message: a 4-byte big-endian length, then the bytes
        while (m_in.size() >= 4 && !m_closed) {
            const uint32_t len = static_cast<uint8_t>(m_in[0]) << 24 | static_cast<uint8_t>(m_in[1]) << 16 |
                                 static_cast<uint8_t>(m_in[2]) << 8 | static_cast<uint8_t>(m_in[3]);
            if (len > 8 * 1024 * 1024) return closeWith("A message too large");
            if (m_in.size() < 4 + len) break;
            const Bytes msg = m_in.substr(4, len);
            m_in.erase(0, 4 + len);
            handle(msg);
        }
    }

    void send(const Bytes& msg) {
        const uint32_t n = static_cast<uint32_t>(msg.size());
        m_out += static_cast<char>(n >> 24);
        m_out += static_cast<char>(n >> 16);
        m_out += static_cast<char>(n >> 8);
        m_out += static_cast<char>(n);
        m_out += msg;
        flush();
    }
    void flush() {
        while (!m_out.empty()) {
            const ssize_t n = ::send(m_fd, m_out.data(), m_out.size(), MSG_NOSIGNAL);
            if (n > 0) {
                m_out.erase(0, static_cast<size_t>(n));
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break; // the rest when the socket can take it (POLLOUT)
        }
    }

    void closeWith(const std::string& error) {
        if (m_closed) return;
        m_closed = true;
        if (auto* tr = t()) {
            if (!error.empty() && tr->state != QuickShare::State::Done && tr->state != QuickShare::State::Declined) {
                tr->state = QuickShare::State::Failed;
                tr->error = error;
                run({"notify-send", "-a", "Quick Share", "-i", "send-to", "Quick Share stopped", tr->sender + ": " + error}, nullptr);
            }
        }
        QuickShare::get().notifyChanged();
        QuickShare::get().finished(this);
    }

    // ── Plain frames ──

    static Bytes offline(int type, const Writer& inner, int field) {
        Writer v1;
        v1.varint(1, static_cast<uint64_t>(type));
        if (field) v1.message(field, inner);
        return Writer().varint(1, 1).message(2, v1).str();
    }

    void handle(const Bytes& msg) {
        switch (m_step) {
        case Step::Request: return onConnectionRequest(msg);
        case Step::ClientInit: return onClientInit(msg);
        case Step::ClientFinish: return onClientFinish(msg);
        case Step::Response: return onConnectionResponse(msg);
        case Step::Secure: return onSecure(msg);
        }
    }

    void onConnectionRequest(const Bytes& msg) {
        Reader frame;
        if (!frame.parse(msg)) return closeWith("Not a Quick Share device");
        auto v1 = frame.message(2);
        if (!v1 || v1->number(1) != CONNECTION_REQUEST) return closeWith("Not a Quick Share device");
        auto req = v1->message(2);
        if (!req) return closeWith("Not a Quick Share device");
        // Endpoint info: a flags byte, 16 bytes, then the name (length first)
        const Bytes info = req->bytes(6);
        std::string name = req->bytes(2);
        if (info.size() > 18) {
            const size_t len = static_cast<uint8_t>(info[17]);
            if (18 + len <= info.size()) name = info.substr(18, len);
        }
        if (auto* tr = t()) tr->sender = name.empty() ? "A device nearby" : name;
        m_step = Step::ClientInit;
        QuickShare::get().notifyChanged();
    }

    // UKEY2: the client commits to its key, we answer with ours
    void onClientInit(const Bytes& msg) {
        Reader ukey;
        if (!ukey.parse(msg) || ukey.number(1) != CLIENT_INIT) return closeWith("The key exchange failed");
        Reader init;
        if (!init.parse(ukey.bytes(2)) || init.number(1) != 1 || init.bytes(2).size() != 32) return closeWith("The key exchange failed");
        for (const auto& c : init.all(3)) {
            Reader commit;
            if (commit.parse(c) && commit.number(1) == kP256Sha512) m_commitment = commit.bytes(2);
        }
        if (m_commitment.empty()) return closeWith("The other device needs a cipher this one lacks");
        m_clientInit = msg;
        Writer key;
        key.varint(1, 1) // EC_P256
            .message(2, Writer().bytes(1, signedBig(m_key.x())).bytes(2, signedBig(m_key.y())));
        Writer serverInit;
        serverInit.varint(1, 1).bytes(2, crypto::random(32)).varint(3, kP256Sha512).bytes(4, key.str());
        m_serverInit = Writer().varint(1, SERVER_INIT).bytes(2, serverInit.str()).str();
        send(m_serverInit);
        m_step = Step::ClientFinish;
    }

    void onClientFinish(const Bytes& msg) {
        Reader ukey;
        if (!ukey.parse(msg) || ukey.number(1) != CLIENT_FINISH) return closeWith("The key exchange failed");
        // The key it committed to, and no other
        if (!crypto::equal(crypto::sha512(msg), m_commitment)) return closeWith("The key exchange failed");
        Reader finish, generic;
        if (!finish.parse(ukey.bytes(2)) || !generic.parse(finish.bytes(1))) return closeWith("The key exchange failed");
        auto point = generic.message(2);
        if (!point) return closeWith("The key exchange failed");
        const Bytes secret = m_key.agree(fit32(point->bytes(1)), fit32(point->bytes(2)));
        if (secret.empty()) return closeWith("The key exchange failed");
        // UKEY2's authentication string and next-protocol secret
        const Bytes derived = crypto::sha256(secret), info = m_clientInit + m_serverInit;
        const Bytes auth = crypto::hkdfSha256(derived, "UKEY2 v1 auth", info, 32);
        const Bytes next = crypto::hkdfSha256(derived, "UKEY2 v1 next", info, 32);
        const Bytes d2dSalt = crypto::sha256("D2D"), smSalt = crypto::sha256("SecureMessage");
        const Bytes clientKey = crypto::hkdfSha256(next, d2dSalt, "client", 32), serverKey = crypto::hkdfSha256(next, d2dSalt, "server", 32);
        m_decryptKey = crypto::hkdfSha256(clientKey, smSalt, "ENC:2", 32);
        m_receiveHmac = crypto::hkdfSha256(clientKey, smSalt, "SIG:1", 32);
        m_encryptKey = crypto::hkdfSha256(serverKey, smSalt, "ENC:2", 32);
        m_sendHmac = crypto::hkdfSha256(serverKey, smSalt, "SIG:1", 32);
        if (auto* tr = t()) tr->pin = pinFrom(auth);
        // Our side of the connection response, in the clear
        Writer resp;
        resp.varint(1, 0).varint(3, 1).message(4, Writer().varint(1, 100)); // status 0, ACCEPT, os: Linux
        send(offline(CONNECTION_RESPONSE, resp, 3));
        m_step = Step::Response;
    }

    void onConnectionResponse(const Bytes& msg) {
        Reader frame;
        if (!frame.parse(msg)) return closeWith("Connection refused");
        auto v1 = frame.message(2);
        if (!v1 || v1->number(1) != CONNECTION_RESPONSE) return closeWith("Connection refused");
        auto resp = v1->message(3);
        if (resp && resp->has(3) && resp->number(3) != 1) return closeWith("The other device declined");
        m_step = Step::Secure;
        // Encrypted from here. The paired key exchange (meaningless without
        // Google's servers: random bytes, as Android accepts)
        Writer pke;
        pke.bytes(1, crypto::random(72)).bytes(2, crypto::random(6));
        sendShare(PAIRED_KEY_ENCRYPTION, pke, 4);
        // Keep-alives, as Android expects every 10 s
        m_keepAlive = EventLoop::get().addTimer(10000, [this] {
            if (!m_closed) sendOffline(KEEP_ALIVE, Writer().boolean(1, false), 6);
        }, true);
    }

    // ── Secure messages ──

    void sendOffline(int type, const Writer& inner, int field) {
        const Bytes frame = offline(type, inner, field);
        const Bytes d2d = Writer().bytes(1, frame).sint(2, ++m_sendSeq).str();
        const Bytes iv = crypto::random(16);
        const Bytes body = crypto::aesCbcEncrypt(m_encryptKey, iv, d2d);
        const Bytes meta = Writer().varint(1, 13).varint(2, 1).str(); // DEVICE_TO_DEVICE_MESSAGE, version 1
        Writer header;
        header.varint(1, 1).varint(2, 2).bytes(5, iv).bytes(6, meta); // HMAC_SHA256, AES_256_CBC
        const Bytes hab = Writer().message(1, header).bytes(2, body).str();
        send(Writer().bytes(1, hab).bytes(2, crypto::hmacSha256(m_sendHmac, hab)).str());
    }

    // A sharing frame as a bytes payload: the whole of it, then an empty
    // last chunk (as Android does)
    void sendShare(int type, const Writer& inner, int field) {
        Writer v1;
        v1.varint(1, static_cast<uint64_t>(type)).message(field, inner);
        const Bytes data = Writer().varint(1, 1).message(2, v1).str();
        int64_t id = 0;
        while (id == 0) id = static_cast<int64_t>(std::mt19937_64{std::random_device{}()}() >> 1);
        Writer header;
        header.sint(1, id).varint(2, kPayloadBytes).sint(3, static_cast<int64_t>(data.size()));
        sendOffline(PAYLOAD_TRANSFER,
                    Writer().varint(1, 1).message(2, header).message(3, Writer().varint(1, 0).sint(2, 0).bytes(3, data)), 4);
        sendOffline(PAYLOAD_TRANSFER,
                    Writer().varint(1, 1).message(2, header).message(3, Writer().varint(1, kLastChunk).sint(2, static_cast<int64_t>(data.size()))),
                    4);
    }

    void onSecure(const Bytes& msg) {
        Reader secure, hab, header, d2d;
        if (!secure.parse(msg)) return closeWith("A damaged message");
        const Bytes habBytes = secure.bytes(1);
        if (!crypto::equal(crypto::hmacSha256(m_receiveHmac, habBytes), secure.bytes(2))) return closeWith("A message failed its check");
        if (!hab.parse(habBytes) || !header.parse(hab.bytes(1))) return closeWith("A damaged message");
        const Bytes plain = crypto::aesCbcDecrypt(m_decryptKey, header.bytes(5), hab.bytes(2));
        if (plain.empty() || !d2d.parse(plain)) return closeWith("A message failed to decrypt");
        // In order, each once
        if (d2d.sint(2) != ++m_receiveSeq) return closeWith("Messages out of order");
        Reader frame;
        if (!frame.parse(d2d.bytes(1))) return closeWith("A damaged message");
        auto v1 = frame.message(2);
        if (!v1) return;
        switch (v1->number(1)) {
        case PAYLOAD_TRANSFER:
            if (auto p = v1->message(4)) onPayload(*p);
            break;
        case KEEP_ALIVE:
            if (auto k = v1->message(6); !k || !k->number(1)) sendOffline(KEEP_ALIVE, Writer().boolean(1, true), 6);
            break;
        case DISCONNECTION: closeWith(m_doneOk ? "" : "The other device cancelled"); break;
        default: break;
        }
    }

    struct Incoming {
        Bytes buffer;    // bytes payloads (sharing frames, texts)
        FILE* out = nullptr;
        std::string path;
        int64_t total = 0, received = 0;
        bool isFile = false;
    };

    void onPayload(const Reader& p) {
        auto header = p.message(2);
        auto chunk = p.message(3);
        if (!header || !chunk) return;
        const int64_t id = header->sint(1);
        const int type = static_cast<int>(header->number(2));
        const Bytes body = chunk->bytes(3);
        const bool last = chunk->number(1) & kLastChunk;
        if (type == kPayloadBytes) {
            auto& in = m_bytes[id];
            in.buffer += body;
            if (in.buffer.size() > 4 * 1024 * 1024) return closeWith("A message too large");
            if (!last) return;
            const Bytes whole = std::move(in.buffer);
            m_bytes.erase(id);
            // A text the user accepted, or a sharing frame
            if (m_textIds.contains(id)) return onText(id, whole);
            return onShare(whole);
        }
        if (type != kPayloadFile) return;
        auto it = m_files.find(id);
        if (it == m_files.end() || !m_accepted) return closeWith("A file nobody asked for");
        auto& f = it->second;
        if (!body.empty()) {
            if (!f.out) return;
            if (std::fwrite(body.data(), 1, body.size(), f.out) != body.size()) return closeWith("Couldn't save: is the disk full?");
            f.received += static_cast<int64_t>(body.size());
            if (auto* tr = t()) tr->received += static_cast<int64_t>(body.size());
            QuickShare::get().notifyChanged();
        }
        if (last) {
            if (f.out) std::fclose(f.out);
            f.out = nullptr;
            if (auto* tr = t()) tr->saved.push_back(f.path);
            ++m_done;
            checkDone();
        }
    }

    void onShare(const Bytes& data) {
        Reader frame;
        if (!frame.parse(data)) return;
        auto v1 = frame.message(2);
        if (!v1) return;
        switch (v1->number(1)) {
        case PAIRED_KEY_ENCRYPTION:
            sendShare(PAIRED_KEY_RESULT, Writer().varint(1, 3), 5); // UNABLE: no Google account here
            break;
        case PAIRED_KEY_RESULT: break;
        case INTRODUCTION: {
            auto intro = v1->message(2);
            if (!intro) return closeWith("A damaged introduction");
            auto* tr = t();
            if (!tr) return;
            for (const auto& fb : intro->all(1)) {
                Reader f;
                if (!f.parse(fb)) continue;
                QuickShare::File file{f.bytes(1), f.has(5) ? f.bytes(5) : "application/octet-stream", f.sint(4), f.sint(3)};
                tr->files.push_back(file);
                tr->total += file.size;
            }
            for (const auto& tb : intro->all(2)) {
                Reader x;
                if (!x.parse(tb)) continue;
                tr->texts.push_back({x.bytes(2), static_cast<int>(x.number(3)), x.sint(4), x.sint(5)});
            }
            if (tr->files.empty() && tr->texts.empty()) {
                // Wi-Fi networks, apps: not taken here
                sendShare(RESPONSE, Writer().varint(1, 4), 3); // UNSUPPORTED_ATTACHMENT_TYPE
                return closeWith("Only files, links and text can be received");
            }
            tr->state = QuickShare::State::Asking;
            QuickShare::get().notifyChanged();
            QuickShare::get().asking.emit(m_id);
            // Unanswered for long: declined
            m_answerTimer = EventLoop::get().addTimer(90000, [this] {
                m_answerTimer = 0;
                if (!m_accepted && !m_closed) decline();
            });
            break;
        }
        case CANCEL: closeWith("The other device cancelled"); break;
        default: break;
        }
    }

    void onText(int64_t id, const Bytes& text) {
        m_textIds.erase(id);
        if (auto* tr = t()) {
            tr->textsReceived.push_back(text);
            // Onto the clipboard; a link opens from the notification
            run({"wl-copy"}, nullptr, text);
            const bool url = text.starts_with("http://") || text.starts_with("https://");
            run({"notify-send", "-a", "Quick Share", "-i", "send-to", url ? "Link received (copied)" : "Text received (copied)",
                 tr->sender + ": " + text.substr(0, 200)},
                nullptr);
        }
        ++m_done;
        checkDone();
    }

    void checkDone() {
        auto* tr = t();
        if (!tr || m_done < m_expected) return;
        tr->state = QuickShare::State::Done;
        m_doneOk = true;
        if (!tr->saved.empty()) {
            const std::string what = tr->saved.size() == 1 ? fs::path(tr->saved.front()).filename().string()
                                                           : std::format("{} files", tr->saved.size());
            run({"notify-send", "-a", "Quick Share", "-i", "send-to", "Received from " + tr->sender, what + " · in Downloads"}, nullptr);
        }
        QuickShare::get().notifyChanged();
        // Done: say so, and leave
        sendOffline(DISCONNECTION, Writer(), 7);
        EventLoop::get().addTimer(500, [this] { closeWith(""); });
    }

    int m_fd;
    int m_id;
    Step m_step = Step::Request;
    Bytes m_in, m_out;
    bool m_closed = false, m_accepted = false, m_doneOk = false;
    crypto::EcKey m_key;
    Bytes m_commitment, m_clientInit, m_serverInit;
    Bytes m_decryptKey, m_receiveHmac, m_encryptKey, m_sendHmac;
    int64_t m_sendSeq = 0, m_receiveSeq = 0;
    std::map<int64_t, Incoming> m_bytes, m_files;
    std::set<int64_t> m_textIds;
    size_t m_done = 0, m_expected = 0;
    EventLoop::TimerId m_keepAlive = 0, m_answerTimer = 0;

    friend class QuickShare;
};

void Connection::accept() {
    auto* tr = t();
    if (!tr || m_closed || m_accepted) return;
    if (m_answerTimer) EventLoop::get().cancel(m_answerTimer);
    m_answerTimer = 0;
    m_accepted = true;
    // Where each file goes, opened now (so a full disk says so at once)
    for (const auto& f : tr->files) {
        Incoming in;
        in.isFile = true;
        in.total = f.size;
        in.path = freePath(f.name);
        in.out = std::fopen(in.path.c_str(), "wb");
        if (!in.out) {
            sendShare(RESPONSE, Writer().varint(1, 3), 3); // NOT_ENOUGH_SPACE (or not writable)
            return closeWith("Couldn't write to Downloads");
        }
        m_files[f.payloadId] = std::move(in);
    }
    for (const auto& x : tr->texts) m_textIds.insert(x.payloadId);
    m_expected = tr->files.size() + tr->texts.size();
    tr->state = QuickShare::State::Receiving;
    sendShare(RESPONSE, Writer().varint(1, 1), 3); // ACCEPT
    QuickShare::get().notifyChanged();
}

void Connection::decline() {
    auto* tr = t();
    if (!tr || m_closed) return;
    tr->state = QuickShare::State::Declined;
    sendShare(RESPONSE, Writer().varint(1, 2), 3); // REJECT
    QuickShare::get().notifyChanged();
    EventLoop::get().addTimer(300, [this] { closeWith(""); });
}

// ── The listening socket ──

class QuickShare::Listener : public EventSource {
public:
    explicit Listener(QuickShare& qs) : m_qs(qs) { EventLoop::get().add(this); }
    ~Listener() override { EventLoop::get().remove(this); }
    int fd() const override { return m_qs.m_listenFd; }
    void dispatch(short revents) override {
        if (revents & POLLIN) m_qs.acceptClient();
    }

private:
    QuickShare& m_qs;
};

QuickShare& QuickShare::get() {
    static QuickShare instance;
    return instance;
}

QuickShare::QuickShare() {
    // Four letters and digits, new each run, as Android's endpoint ids
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::mt19937 rng{std::random_device{}()};
    for (int i = 0; i < 4; ++i) m_endpointId += abc[rng() % 36];
    apply();
    Config::get().changed.connectForever([this] { apply(); });
}

std::string QuickShare::deviceName() const {
    std::string name = Config::get().value<std::string>("/quickshare/name", "");
    if (!name.empty()) return name;
    char host[256] = {};
    gethostname(host, sizeof host - 1);
    return host;
}

void QuickShare::apply() {
    const bool on = Config::get().value<bool>("/quickshare/receive", true);
    const std::string key = std::format("{}|{}", on, deviceName());
    if (key == m_applied) return;
    m_applied = key;
    stop();
    if (on) start();
    changed.emit();
}

void QuickShare::start() {
    m_listenFd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_listenFd < 0) {
        m_status = "Couldn't open a network socket";
        return;
    }
    int no = 0, yes = 1;
    setsockopt(m_listenFd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof no); // IPv4 too
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = 0; // any free port: the advertisement says which
    socklen_t len = sizeof addr;
    if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(m_listenFd, 4) < 0 ||
        getsockname(m_listenFd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        close(m_listenFd);
        m_listenFd = -1;
        m_status = "Couldn't listen on the network";
        return;
    }
    m_port = ntohs(addr.sin6_port);
    m_listener = std::make_unique<Listener>(*this);
    m_status.clear();
    advertise();
    log::info("quick share: listening on port {}", m_port);
}

void QuickShare::stop() {
    m_mdns.reset(); // a goodbye on the network
    m_listener.reset();
    if (m_listenFd >= 0) close(m_listenFd);
    m_listenFd = -1;
}

void QuickShare::advertise() {
    // The service name: 0x23, the endpoint id, the service id, two zeros
    Bytes name = "\x23" + m_endpointId + std::string("\xFC\x9F\x5E\x00\x00", 5);
    // The endpoint info: a laptop (3 << 1), visible; 16 random bytes; the name
    const std::string device = deviceName().substr(0, 60);
    Bytes info = std::string(1, static_cast<char>(3 << 1)) + crypto::random(16) + std::string(1, static_cast<char>(device.size())) + device;
    m_mdns = std::make_unique<MdnsResponder>(base64url(name), kServiceType, static_cast<uint16_t>(m_port),
                                             std::vector<std::string>{"n=" + base64url(info)});
    if (!m_mdns->ok()) {
        m_mdns.reset();
        m_status = "Not visible: the network's mDNS port can't be used";
    }
}

void QuickShare::acceptClient() {
    for (;;) {
        const int fd = accept4(m_listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        int yes = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
        auto& tr = newTransfer();
        m_connections.push_back(std::make_unique<Connection>(fd, tr));
        changed.emit();
    }
}

QuickShare::Transfer& QuickShare::newTransfer() {
    Transfer t;
    t.id = m_nextId++;
    // Only the last few are kept
    while (m_transfers.size() > 8) m_transfers.erase(m_transfers.begin());
    return m_transfers[t.id] = t;
}

const QuickShare::Transfer* QuickShare::transfer(int id) const {
    const auto it = m_transfers.find(id);
    return it == m_transfers.end() ? nullptr : &it->second;
}

void QuickShare::accept(int id) {
    for (auto& c : m_connections)
        if (c->id() == id) c->accept();
}

void QuickShare::decline(int id) {
    for (auto& c : m_connections)
        if (c->id() == id) c->decline();
}

void QuickShare::finished(Connection* c) {
    EventLoop::get().defer([this, c] {
        std::erase_if(m_connections, [c](const std::unique_ptr<Connection>& p) { return p.get() == c; });
    });
}

} // namespace m3::quickshare
