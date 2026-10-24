#include "quickshare/QuickShare.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "quickshare/Ble.h"
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

Bytes unbase64url(const std::string& in) {
    Bytes out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
              : c == '-' || c == '+' ? 62 : c == '_' || c == '/' ? 63 : -1;
        if (v < 0) continue; // padding
        acc = acc << 6 | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>(acc >> bits & 0xFF);
        }
    }
    return out;
}

// What the other side shows for a file: its MIME type, and a kind
std::string mimeOf(const std::string& path) {
    static const std::map<std::string, std::string> types{
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".png", "image/png"}, {".gif", "image/gif"}, {".webp", "image/webp"},
        {".heic", "image/heic"}, {".svg", "image/svg+xml"}, {".mp4", "video/mp4"}, {".mkv", "video/x-matroska"},
        {".webm", "video/webm"}, {".mov", "video/quicktime"}, {".mp3", "audio/mpeg"}, {".flac", "audio/flac"},
        {".ogg", "audio/ogg"}, {".opus", "audio/opus"}, {".m4a", "audio/mp4"}, {".wav", "audio/wav"},
        {".pdf", "application/pdf"}, {".zip", "application/zip"}, {".apk", "application/vnd.android.package-archive"},
        {".txt", "text/plain"}, {".md", "text/markdown"}, {".html", "text/html"}, {".csv", "text/csv"}, {".json", "application/json"},
        {".doc", "application/msword"}, {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".odt", "application/vnd.oasis.opendocument.text"}};
    std::string ext = fs::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto it = types.find(ext);
    return it == types.end() ? "application/octet-stream" : it->second;
}

int kindOf(const std::string& mime) {
    if (mime.starts_with("image/")) return 1;
    if (mime.starts_with("video/")) return 2;
    if (mime == "application/vnd.android.package-archive") return 3;
    if (mime.starts_with("audio/")) return 4;
    return 0;
}

int64_t randomId() {
    static std::mt19937_64 rng{std::random_device{}()};
    int64_t id = 0;
    while (id == 0) id = static_cast<int64_t>(rng() >> 1);
    return id;
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

// The QR code: a P-256 key of its own, its X compressed behind two version
// bytes. The phone that scans it advertises a token derived from it (or its
// name sealed with it, when hidden), so it's found without Bluetooth; our
// signature over the handshake with this key lets it skip its prompt.
struct QuickShare::Qr {
    crypto::EcKey key;
    Bytes token, nameKey;
    std::string url;
};

// ── A connection from a phone: the handshake, then the transfer ──

class Connection : public EventSource {
public:
    // A phone connecting to us
    Connection(int fd, QuickShare::Transfer& t) : m_fd(fd), m_id(t.id) { EventLoop::get().add(this); }
    // Us connecting to it, to send these (fd: connecting); request: our
    // connection request
    Connection(int fd, QuickShare::Transfer& t, Bytes request, std::vector<std::string> paths)
        : m_fd(fd), m_id(t.id), m_step(Step::Connect), m_client(true), m_request(std::move(request)) {
        for (size_t i = 0; i < paths.size() && i < t.files.size(); ++i) {
            Outgoing o;
            o.path = paths[i];
            o.payloadId = t.files[i].payloadId;
            o.size = t.files[i].size;
            m_outgoing.push_back(std::move(o));
        }
        EventLoop::get().add(this);
        m_answerTimer = EventLoop::get().addTimer(10000, [this] {
            m_answerTimer = 0;
            if (m_step == Step::Connect) closeWith("The other device didn't answer");
        });
    }
    ~Connection() override {
        EventLoop::get().remove(this);
        if (m_keepAlive) EventLoop::get().cancel(m_keepAlive);
        if (m_answerTimer) EventLoop::get().cancel(m_answerTimer);
        for (auto& [_, f] : m_files)
            if (f.out) std::fclose(f.out);
        for (auto& o : m_outgoing)
            if (o.in) std::fclose(o.in);
        if (m_fd >= 0) close(m_fd);
    }

    int fd() const override { return m_fd; }
    short events() const override {
        const bool more = m_step == Step::Connect || !m_out.empty() || (m_pumping && m_next < m_outgoing.size());
        return static_cast<short>(POLLIN | (more ? POLLOUT : 0));
    }
    void dispatch(short revents) override {
        if (m_closed) return;
        if (m_step == Step::Connect) {
            if (!(revents & (POLLOUT | POLLERR | POLLHUP))) return;
            int err = 0;
            socklen_t len = sizeof err;
            getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err) return closeWith("Couldn't reach the other device");
            return startClient();
        }
        if (revents & POLLOUT) {
            flush();
            pump();
        }
        if (revents & (POLLIN | POLLHUP | POLLERR)) readSome();
    }

    void accept();
    void decline();
    void cancel();
    int id() const { return m_id; }

private:
    // A server goes Request → ClientInit → ClientFinish; a client
    // Connect → ServerInit; then both Response → Secure
    enum class Step { Connect, Request, ServerInit, ClientInit, ClientFinish, Response, Secure };

    QuickShare::Transfer* t() { return const_cast<QuickShare::Transfer*>(QuickShare::get().transfer(m_id)); }

    void readSome() {
        char buf[65536];
        for (;;) {
            const ssize_t n = recv(m_fd, buf, sizeof buf, 0);
            if (n > 0) {
                m_in.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) {
                if (m_client && m_allSent) finishSending();
                return closeWith(m_doneOk ? "" : "The other device left");
            }
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
                run({"notify-send", "-a", "Quick Share", "-i", "material3ui-quickshare", "Quick Share stopped", tr->sender + ": " + error}, nullptr);
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
        case Step::Connect: return;
        case Step::Request: return onConnectionRequest(msg);
        case Step::ServerInit: return onServerInit(msg);
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
        if (!deriveKeys(*point)) return closeWith("The key exchange failed");
        sendConnectionResponse();
    }

    // The shared secret, then UKEY2's authentication string (the PIN) and
    // the four message keys; a client's are a server's, swapped
    bool deriveKeys(const Reader& point) {
        const Bytes secret = m_key.agree(fit32(point.bytes(1)), fit32(point.bytes(2)));
        if (secret.empty()) return false;
        const Bytes derived = crypto::sha256(secret), info = m_clientInit + m_serverInit;
        const Bytes auth = crypto::hkdfSha256(derived, "UKEY2 v1 auth", info, 32);
        const Bytes next = crypto::hkdfSha256(derived, "UKEY2 v1 next", info, 32);
        const Bytes d2dSalt = crypto::sha256("D2D"), smSalt = crypto::sha256("SecureMessage");
        const Bytes clientKey = crypto::hkdfSha256(next, d2dSalt, "client", 32), serverKey = crypto::hkdfSha256(next, d2dSalt, "server", 32);
        const Bytes& theirs = m_client ? serverKey : clientKey;
        const Bytes& ours = m_client ? clientKey : serverKey;
        m_decryptKey = crypto::hkdfSha256(theirs, smSalt, "ENC:2", 32);
        m_receiveHmac = crypto::hkdfSha256(theirs, smSalt, "SIG:1", 32);
        m_encryptKey = crypto::hkdfSha256(ours, smSalt, "ENC:2", 32);
        m_sendHmac = crypto::hkdfSha256(ours, smSalt, "SIG:1", 32);
        if (auto* tr = t()) tr->pin = pinFrom(auth);
        m_auth = auth;
        return true;
    }

    // Our side of the connection response, in the clear
    void sendConnectionResponse() {
        Writer resp;
        resp.varint(1, 0).varint(3, 1).message(4, Writer().varint(1, 100)); // status 0, ACCEPT, os: Linux
        send(offline(CONNECTION_RESPONSE, resp, 3));
        m_step = Step::Response;
        QuickShare::get().notifyChanged();
    }

    // ── As the client ──

    void startClient() {
        if (m_answerTimer) EventLoop::get().cancel(m_answerTimer);
        m_answerTimer = 0;
        send(m_request);
        // Commit to our finish (our key), then say hello
        Writer point;
        point.bytes(1, signedBig(m_key.x())).bytes(2, signedBig(m_key.y()));
        const Bytes generic = Writer().varint(1, 1).message(2, point).str(); // EC_P256
        m_clientFinish = Writer().varint(1, CLIENT_FINISH).bytes(2, Writer().bytes(1, generic).str()).str();
        Writer commitment;
        commitment.varint(1, kP256Sha512).bytes(2, crypto::sha512(m_clientFinish));
        Writer init;
        init.varint(1, 1).bytes(2, crypto::random(32)).message(3, commitment).bytes(4, "AES_256_CBC-HMAC_SHA256");
        m_clientInit = Writer().varint(1, CLIENT_INIT).bytes(2, init.str()).str();
        send(m_clientInit);
        m_step = Step::ServerInit;
    }

    void onServerInit(const Bytes& msg) {
        Reader ukey, init, key;
        if (!ukey.parse(msg) || ukey.number(1) != SERVER_INIT) return closeWith("The other device refused the connection");
        if (!init.parse(ukey.bytes(2)) || init.number(3) != kP256Sha512 || !key.parse(init.bytes(4)))
            return closeWith("The key exchange failed");
        auto point = key.message(2);
        m_serverInit = msg;
        if (!point || !deriveKeys(*point)) return closeWith("The key exchange failed");
        send(m_clientFinish);
        sendConnectionResponse();
    }

    // What we offer, once the paired key frames are through
    void introduce() {
        auto* tr = t();
        if (!tr || m_introduced) return;
        m_introduced = true;
        Writer intro;
        for (const auto& f : tr->files) {
            Writer meta;
            meta.bytes(1, f.name).varint(2, static_cast<uint64_t>(kindOf(f.mime))).sint(3, f.payloadId).sint(4, f.size).bytes(5, f.mime).sint(6, randomId());
            intro.message(1, meta);
        }
        sendShare(INTRODUCTION, intro, 2);
        tr->state = QuickShare::State::Asking;
        QuickShare::get().notifyChanged();
    }

    // Files out in 512 KB chunks, a little ahead of the socket at a time
    void pump() {
        if (!m_pumping) return;
        while (m_out.size() < 1024 * 1024 && m_next < m_outgoing.size() && !m_closed) {
            auto& o = m_outgoing[m_next];
            if (!o.in) {
                o.in = std::fopen(o.path.c_str(), "rb");
                if (!o.in) return closeWith("Couldn't read " + fs::path(o.path).filename().string());
            }
            Bytes chunk(512 * 1024, '\0');
            const size_t n = std::fread(chunk.data(), 1, std::min<int64_t>(static_cast<int64_t>(chunk.size()), o.size - o.offset), o.in);
            chunk.resize(n);
            Writer header;
            header.sint(1, o.payloadId).varint(2, kPayloadFile).sint(3, o.size);
            if (n > 0) {
                sendOffline(PAYLOAD_TRANSFER, Writer().varint(1, 1).message(2, header).message(3, Writer().varint(1, 0).sint(2, o.offset).bytes(3, chunk)), 4);
                o.offset += static_cast<int64_t>(n);
                if (auto* tr = t()) tr->received += static_cast<int64_t>(n);
            }
            if (n == 0 || o.offset >= o.size) {
                // The file changed size under us: what was read is what goes
                sendOffline(PAYLOAD_TRANSFER, Writer().varint(1, 1).message(2, header).message(3, Writer().varint(1, kLastChunk).sint(2, o.offset)), 4);
                std::fclose(o.in);
                o.in = nullptr;
                ++m_next;
            }
        }
        QuickShare::get().notifyChanged();
        if (m_next >= m_outgoing.size() && !m_allSent) {
            m_allSent = true;
            // The other side says goodbye when it has everything; if it
            // doesn't, it had it anyway
            m_answerTimer = EventLoop::get().addTimer(15000, [this] {
                m_answerTimer = 0;
                finishSending();
                sendOffline(DISCONNECTION, Writer(), 7);
                EventLoop::get().addTimer(300, [this] { closeWith(""); });
            });
        }
    }

    void finishSending() {
        auto* tr = t();
        if (!tr || m_doneOk) return;
        m_doneOk = true;
        tr->state = QuickShare::State::Done;
        const std::string what = tr->files.size() == 1 ? tr->files.front().name : std::format("{} files", tr->files.size());
        run({"notify-send", "-a", "Quick Share", "-i", "material3ui-quickshare", "Sent to " + tr->sender, what}, nullptr);
        QuickShare::get().notifyChanged();
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
        // Sent to a phone that scanned our QR code: proof, so it needn't ask
        if (m_client) {
            const Bytes qr = QuickShare::get().qrSignature(m_auth);
            if (!qr.empty()) pke.bytes(4, qr);
        }
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
        v1.varint(1, static_cast<uint64_t>(type));
        if (field) v1.message(field, inner);
        const Bytes data = Writer().varint(1, 1).message(2, v1).str();
        const int64_t id = randomId();
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
        case DISCONNECTION:
            if (m_client && m_allSent) finishSending();
            closeWith(m_doneOk ? "" : "The other device cancelled");
            break;
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
        case PAIRED_KEY_RESULT:
            if (m_client) introduce();
            break;
        case RESPONSE: {
            // Their answer to what we offered
            auto* tr = t();
            auto resp = v1->message(3);
            if (!m_client || !tr || !m_introduced || m_pumping) return;
            const int status = resp ? static_cast<int>(resp->number(1)) : 0;
            if (status != 1) {
                tr->state = QuickShare::State::Declined;
                QuickShare::get().notifyChanged();
                const char* why = status == 3 ? "There isn't enough space on the other device"
                                : status == 4 ? "The other device can't take these files"
                                : status == 5 ? "The other device didn't answer in time"
                                              : "";
                if (*why) {
                    tr->state = QuickShare::State::Failed;
                    tr->error = why;
                }
                m_doneOk = true; // nothing more to say
                return closeWith("");
            }
            tr->state = QuickShare::State::Receiving;
            m_pumping = true;
            pump();
            break;
        }
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
            run({"notify-send", "-a", "Quick Share", "-i", "material3ui-quickshare", url ? "Link received (copied)" : "Text received (copied)",
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
            run({"notify-send", "-a", "Quick Share", "-i", "material3ui-quickshare", "Received from " + tr->sender, what + " · in Downloads"}, nullptr);
        }
        QuickShare::get().notifyChanged();
        // Done: say so, and leave
        sendOffline(DISCONNECTION, Writer(), 7);
        EventLoop::get().addTimer(500, [this] { closeWith(""); });
    }

    struct Outgoing {
        std::string path;
        int64_t payloadId = 0, size = 0, offset = 0;
        FILE* in = nullptr;
    };

    int m_fd;
    int m_id;
    Step m_step = Step::Request;
    bool m_client = false, m_introduced = false, m_pumping = false, m_allSent = false;
    Bytes m_request, m_clientFinish, m_auth;
    std::vector<Outgoing> m_outgoing;
    size_t m_next = 0;
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

void Connection::cancel() {
    auto* tr = t();
    if (!tr || m_closed) return;
    if (m_step == Step::Secure) {
        sendShare(CANCEL, Writer(), 0);
        sendOffline(DISCONNECTION, Writer(), 7);
    }
    tr->state = QuickShare::State::Failed;
    tr->error = "Cancelled";
    m_doneOk = true;
    QuickShare::get().notifyChanged();
    EventLoop::get().addTimer(300, [this] { closeWith(""); });
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

std::string QuickShare::symbol() {
    static const std::string path = paths::dataFile("icons/quickshare-symbol.svg");
    return path.empty() ? "nearby" : path;
}

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
    const std::string key = std::format("{}|{}|{}", on, deviceName(), port());
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
    // A fixed port, so a firewall can let it through; any free one if taken
    addr.sin6_port = htons(static_cast<uint16_t>(port()));
    socklen_t len = sizeof addr;
    if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        addr.sin6_port = 0;
        bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    }
    if (listen(m_listenFd, 4) < 0 ||
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
    checkFirewall();
    log::info("quick share: listening on port {}", m_port);
}

void QuickShare::stop() {
    m_mdns.reset(); // a goodbye on the network
    m_listener.reset();
    if (m_listenFd >= 0) close(m_listenFd);
    m_listenFd = -1;
}

// The service name: 0x23, the endpoint id, the service id, two zeros
std::string QuickShare::instanceName() const {
    return base64url("\x23" + m_endpointId + std::string("\xFC\x9F\x5E\x00\x00", 5));
}

// The endpoint info: a laptop (3 << 1), visible; 16 random bytes; the name
std::string QuickShare::endpointInfo() const {
    const std::string device = deviceName().substr(0, 60);
    return std::string(1, static_cast<char>(3 << 1)) + crypto::random(16) + std::string(1, static_cast<char>(device.size())) + device;
}

void QuickShare::advertise() {
    m_mdns = std::make_unique<MdnsResponder>(instanceName(), kServiceType, static_cast<uint16_t>(m_port),
                                             std::vector<std::string>{"n=" + base64url(endpointInfo())});
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

void QuickShare::cancel(int id) {
    for (auto& c : m_connections)
        if (c->id() == id) c->cancel();
}

QuickShare::Lease QuickShare::discover() {
    if (m_discoverers++ == 0) {
        m_beacon = std::make_unique<BleBeacon>();
        m_browser = std::make_unique<MdnsBrowser>(kServiceType);
        m_browser->changed = [this] { changed.emit(); };
    }
    return std::shared_ptr<void>(nullptr, [](void*) {
        auto& qs = QuickShare::get();
        if (--qs.m_discoverers > 0) return;
        // A moment later, so closing and reopening doesn't flap Bluetooth
        EventLoop::get().defer([] {
            auto& q = QuickShare::get();
            if (q.m_discoverers > 0) return;
            q.m_browser.reset();
            q.m_beacon.reset();
            q.m_qr.reset();
            q.changed.emit();
        });
    });
}

std::vector<QuickShare::Device> QuickShare::devices() const {
    std::vector<Device> out;
    if (!m_browser) return out;
    const std::string self = instanceName();
    for (const auto& [label, inst] : m_browser->instances()) {
        if (label == self || !inst.port || !inst.addr) continue;
        Device d;
        d.id = label;
        d.addr = inst.addr;
        d.port = inst.port;
        for (const auto& t : inst.txt) {
            if (!t.starts_with("n=")) continue;
            const Bytes info = unbase64url(t.substr(2));
            if (info.empty()) continue;
            d.type = static_cast<uint8_t>(info[0]) >> 1 & 7;
            const bool hidden = static_cast<uint8_t>(info[0]) & 0x10; // no name in the clear
            size_t at = 17;
            if (!hidden && info.size() > 18) {
                const size_t len = static_cast<uint8_t>(info[17]);
                if (18 + len <= info.size()) d.name = info.substr(18, len);
                at = 18 + len;
            }
            // Then TLV records: type 1 is the QR code's, when it scanned ours
            while (m_qr && at + 2 <= info.size()) {
                const int type = static_cast<uint8_t>(info[at]);
                const size_t len = static_cast<uint8_t>(info[at + 1]);
                at += 2;
                if (at + len > info.size()) break;
                const Bytes value = info.substr(at, len);
                at += len;
                if (type != 1) continue;
                if (crypto::equal(value, m_qr->token)) {
                    d.scannedQr = true;
                } else if (value.size() > 28) {
                    // A hidden device: its name, sealed with the QR code's key
                    const Bytes name = crypto::aesGcmDecrypt(m_qr->nameKey, value, m_qr->token);
                    if (!name.empty()) {
                        d.scannedQr = true;
                        d.name = name;
                    }
                }
            }
            if (hidden && !d.scannedQr) d.name.clear();
        }
        if (d.name.empty()) d.name = d.type == 1 ? "A phone nearby" : d.type == 2 ? "A tablet nearby" : "A device nearby";
        if (std::ranges::any_of(out, [&](const Device& o) { return o.id == d.id; })) continue;
        out.push_back(std::move(d));
    }
    std::ranges::sort(out, {}, &Device::name);
    return out;
}

QuickShare::~QuickShare() = default;

std::string QuickShare::qrUrl() {
    if (!m_qr) {
        m_qr = std::make_unique<Qr>();
        if (!m_qr->key.valid()) {
            m_qr.reset();
            return {};
        }
        const bool oddY = !m_qr->key.y().empty() && (static_cast<uint8_t>(m_qr->key.y().back()) & 1);
        const Bytes data = std::string("\0\0", 2) + static_cast<char>(oddY ? 3 : 2) + m_qr->key.x();
        m_qr->token = crypto::hkdfSha256(data, "", "advertisingContext", 16);
        m_qr->nameKey = crypto::hkdfSha256(data, "", "encryptionKey", 16);
        m_qr->url = "https://quickshare.google/qrcode#key=" + base64url(data);
    }
    return m_qr->url;
}

Bytes QuickShare::qrSignature(const Bytes& authKey) const {
    return m_qr ? m_qr->key.sign(authKey) : Bytes{};
}

int QuickShare::port() const {
    return std::clamp(Config::get().value<int>("/quickshare/port", 47893), 1024, 65535);
}

// Incoming connections dropped by a firewall look, from the phone, like a
// device that can't take files. ufw keeps its rules readable; firewalld
// answers a query without root.
void QuickShare::checkFirewall() {
    const int p = port();
    run({"sh", "-c", std::format(
        "if systemctl is-active -q ufw; then "
        "  policy=$(sed -n 's/^DEFAULT_INPUT_POLICY=\"\\(.*\\)\"/\\1/p' /etc/default/ufw); "
        "  if [ \"$policy\" = ACCEPT ] || grep -qsE -- '--dport {0}( |$).*ACCEPT|--dports [0-9,:]*{0}' /etc/ufw/user.rules; then echo 'ufw open'; else echo 'ufw blocked'; fi; "
        "elif systemctl is-active -q firewalld; then "
        "  if firewall-cmd --query-port={0}/tcp >/dev/null 2>&1; then echo 'firewalld open'; else echo 'firewalld blocked'; fi; "
        "else echo none; fi", p)},
        [this](int, const std::string& out) {
            m_firewall = out.starts_with("ufw") ? "ufw" : out.starts_with("firewalld") ? "firewalld" : "";
            m_firewallBlocks = out.find("blocked") != std::string::npos;
            changed.emit();
        });
}

void QuickShare::allowThroughFirewall() {
    const int p = port();
    std::string cmd;
    if (m_firewall == "ufw") cmd = std::format("ufw allow {}/tcp comment 'Quick Share'", p);
    else if (m_firewall == "firewalld") cmd = std::format("firewall-cmd --permanent --add-port={0}/tcp && firewall-cmd --add-port={0}/tcp", p);
    if (cmd.empty()) return;
    run({"pkexec", "sh", "-c", cmd}, [this](int, const std::string&) { checkFirewall(); });
}

std::string QuickShare::discoveryNote() const {
    if (m_browser && !m_browser->ok()) return "The network's mDNS port can't be used";
    if (m_beacon && !m_beacon->error().empty())
        return m_beacon->error() + ": phones show up only with their Quick Share open to “Everyone”, on this network";
    return {};
}

int QuickShare::send(const Device& to, const std::vector<std::string>& paths) {
    auto& tr = newTransfer();
    tr.outgoing = true;
    tr.sender = to.name;
    std::vector<std::string> kept;
    for (const auto& p : paths) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) continue;
        File f;
        f.name = fs::path(p).filename().string();
        f.mime = mimeOf(p);
        f.size = static_cast<int64_t>(fs::file_size(p, ec));
        f.payloadId = randomId();
        tr.files.push_back(f);
        tr.total += f.size;
        kept.push_back(p);
    }
    if (kept.empty()) {
        m_transfers.erase(tr.id);
        return 0;
    }
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(to.port);
    addr.sin_addr.s_addr = to.addr;
    if (fd < 0 || (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 && errno != EINPROGRESS)) {
        if (fd >= 0) close(fd);
        tr.state = State::Failed;
        tr.error = "Couldn't reach " + to.name;
        changed.emit();
        return tr.id;
    }
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
    // Our connection request: who we are, over Wi-Fi
    const std::string device = deviceName().substr(0, 60);
    Writer req;
    req.bytes(1, m_endpointId).bytes(2, device).varint(5, 5).bytes(6, endpointInfo()); // medium: WIFI_LAN
    Writer v1;
    v1.varint(1, CONNECTION_REQUEST).message(2, req);
    const Bytes request = Writer().varint(1, 1).message(2, v1).str();
    m_connections.push_back(std::make_unique<Connection>(fd, tr, request, kept));
    changed.emit();
    return tr.id;
}

void QuickShare::finished(Connection* c) {
    EventLoop::get().defer([this, c] {
        std::erase_if(m_connections, [c](const std::unique_ptr<Connection>& p) { return p.get() == c; });
    });
}

} // namespace m3::quickshare
