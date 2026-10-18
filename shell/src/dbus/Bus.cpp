#include "dbus/Bus.h"

#include "core/Log.h"

#include <cstring>
#include <ctime>
#include <systemd/sd-bus.h>

namespace m3::dbus {

namespace {

bool readNext(sd_bus_message* m, Value& out);

template <typename T>
bool readBasic(sd_bus_message* m, char type, Value& out) {
    T v{};
    if (sd_bus_message_read_basic(m, type, &v) < 0) return false;
    out = v;
    return true;
}

bool readNext(sd_bus_message* m, Value& out) {
    char type = 0;
    const char* contents = nullptr;
    if (sd_bus_message_peek_type(m, &type, &contents) <= 0) return false;

    switch (type) {
    case SD_BUS_TYPE_ARRAY: {
        if (contents[0] == SD_BUS_TYPE_BYTE) {
            const void* data = nullptr;
            size_t size = 0;
            if (sd_bus_message_read_array(m, SD_BUS_TYPE_BYTE, &data, &size) < 0) return false;
            const auto* b = static_cast<const uint8_t*>(data);
            out = Value::binary(std::vector<uint8_t>(b, b + size));
            return true;
        }
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, contents) < 0) return false;
        if (contents[0] == SD_BUS_TYPE_DICT_ENTRY_BEGIN) {
            // a{kv}: contents is "{kv}", entries are entered as "kv"
            const std::string inner(contents + 1, std::strlen(contents) - 2);
            out = Value::object();
            while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, inner.c_str()) > 0) {
                Value key, val;
                readNext(m, key);
                readNext(m, val);
                out[key.is_string() ? key.get<std::string>() : key.dump()] = std::move(val);
                sd_bus_message_exit_container(m);
            }
        } else {
            out = Value::array();
            Value v;
            while (readNext(m, v)) out.push_back(std::move(v));
        }
        sd_bus_message_exit_container(m);
        return true;
    }
    case SD_BUS_TYPE_VARIANT:
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, contents) < 0) return false;
        readNext(m, out);
        sd_bus_message_exit_container(m);
        return true;
    case SD_BUS_TYPE_STRUCT: {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_STRUCT, contents) < 0) return false;
        out = Value::array();
        Value v;
        while (readNext(m, v)) out.push_back(std::move(v));
        sd_bus_message_exit_container(m);
        return true;
    }
    case SD_BUS_TYPE_BYTE: return readBasic<uint8_t>(m, type, out);
    case SD_BUS_TYPE_BOOLEAN: {
        int v = 0;
        if (sd_bus_message_read_basic(m, type, &v) < 0) return false;
        out = v != 0;
        return true;
    }
    case SD_BUS_TYPE_INT16: return readBasic<int16_t>(m, type, out);
    case SD_BUS_TYPE_UINT16: return readBasic<uint16_t>(m, type, out);
    case SD_BUS_TYPE_INT32: return readBasic<int32_t>(m, type, out);
    case SD_BUS_TYPE_UINT32: return readBasic<uint32_t>(m, type, out);
    case SD_BUS_TYPE_INT64: return readBasic<int64_t>(m, type, out);
    case SD_BUS_TYPE_UINT64: return readBasic<uint64_t>(m, type, out);
    case SD_BUS_TYPE_DOUBLE: return readBasic<double>(m, type, out);
    case SD_BUS_TYPE_UNIX_FD: return readBasic<int>(m, type, out);
    case SD_BUS_TYPE_STRING:
    case SD_BUS_TYPE_OBJECT_PATH:
    case SD_BUS_TYPE_SIGNATURE: {
        const char* s = nullptr;
        if (sd_bus_message_read_basic(m, type, &s) < 0) return false;
        out = s ? s : "";
        return true;
    }
    default:
        sd_bus_message_skip(m, nullptr);
        out = nullptr;
        return true;
    }
}

int onReply(sd_bus_message* m, void* userdata, sd_bus_error*) {
    std::unique_ptr<Bus::Reply> reply(static_cast<Bus::Reply*>(userdata));
    if (!*reply) return 0;
    if (sd_bus_message_is_method_error(m, nullptr)) {
        const sd_bus_error* e = sd_bus_message_get_error(m);
        (*reply)(Value::array(), e && e->name ? e->name : "error");
    } else {
        (*reply)(readAll(m), "");
    }
    return 0;
}

int onSignal(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto& handler = *static_cast<Bus::Handler*>(userdata);
    handler(readAll(m), m);
    sd_bus_message_rewind(m, 1);
    return 0;
}

} // namespace

Value readAll(sd_bus_message* m) {
    Value out = Value::array();
    Value v;
    while (readNext(m, v)) out.push_back(std::move(v));
    return out;
}

namespace detail {
void append(sd_bus_message* m, const std::string& v) { sd_bus_message_append(m, "s", v.c_str()); }
void append(sd_bus_message* m, const char* v) { sd_bus_message_append(m, "s", v); }
void append(sd_bus_message* m, bool v) { sd_bus_message_append(m, "b", static_cast<int>(v)); }
void append(sd_bus_message* m, int32_t v) { sd_bus_message_append(m, "i", v); }
void append(sd_bus_message* m, uint32_t v) { sd_bus_message_append(m, "u", v); }
void append(sd_bus_message* m, double v) { sd_bus_message_append(m, "d", v); }
void append(sd_bus_message* m, const ObjectPath& v) { sd_bus_message_append(m, "o", v.path.c_str()); }
} // namespace detail

Writer variant(bool v) {
    return [v](sd_bus_message* m) { sd_bus_message_append(m, "v", "b", static_cast<int>(v)); };
}

Subscription& Subscription::operator=(Subscription&& o) noexcept {
    if (this != &o) {
        if (m_slot) sd_bus_slot_unref(m_slot);
        m_slot = o.m_slot;
        o.m_slot = nullptr;
    }
    return *this;
}

Subscription::~Subscription() {
    if (m_slot) sd_bus_slot_unref(m_slot);
}

Bus& Bus::system() {
    static Bus instance(true);
    return instance;
}

Bus& Bus::session() {
    static Bus instance(false);
    return instance;
}

Bus::Bus(bool system) {
    const int r = system ? sd_bus_open_system(&m_bus) : sd_bus_open_user(&m_bus);
    if (r < 0) {
        log::warn("cannot connect to the {} bus: {}", system ? "system" : "session", std::strerror(-r));
        m_bus = nullptr;
        return;
    }
    EventLoop::get().add(this);
}

Bus::~Bus() {
    if (m_bus) sd_bus_flush_close_unref(m_bus);
}

std::string Bus::uniqueName() const {
    const char* n = nullptr;
    if (m_bus) sd_bus_get_unique_name(m_bus, &n);
    return n ? n : "";
}

void Bus::call(const std::string& dest, const std::string& path, const std::string& iface, const std::string& method,
               const Writer& write, Reply reply) {
    if (!m_bus) {
        if (reply) reply(Value::array(), "org.freedesktop.DBus.Error.NoServer");
        return;
    }
    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call(m_bus, &m, dest.c_str(), path.c_str(), iface.c_str(), method.c_str()) < 0) return;
    if (write) write(m);
    auto* userdata = new Reply(std::move(reply));
    // A null slot makes the call "floating": the bus owns it until the reply
    if (sd_bus_call_async(m_bus, nullptr, m, onReply, userdata, 0) < 0) {
        if (*userdata) (*userdata)(Value::array(), "org.freedesktop.DBus.Error.Failed");
        delete userdata;
    }
    sd_bus_message_unref(m);
}

void Bus::getAll(const std::string& dest, const std::string& path, const std::string& iface,
                 std::function<void(const Value&, bool)> done) {
    call(dest, path, "org.freedesktop.DBus.Properties", "GetAll", args(iface),
         [done = std::move(done)](const Value& a, const std::string& err) {
             const bool ok = err.empty() && !a.empty() && a[0].is_object();
             done(ok ? a[0] : Value::object(), ok);
         });
}

void Bus::setProperty(const std::string& dest, const std::string& path, const std::string& iface,
                      const std::string& name, const Writer& value) {
    call(dest, path, "org.freedesktop.DBus.Properties", "Set", [&](sd_bus_message* m) {
        sd_bus_message_append(m, "ss", iface.c_str(), name.c_str());
        value(m);
    });
}

Subscription Bus::subscribe(const std::string& sender, const std::string& path, const std::string& iface,
                            const std::string& member, Handler handler) {
    if (!m_bus) return {};
    auto nul = [](const std::string& s) { return s.empty() ? nullptr : s.c_str(); };
    sd_bus_slot* slot = nullptr;
    auto* userdata = new Handler(std::move(handler));
    if (sd_bus_match_signal_async(m_bus, &slot, nul(sender), nul(path), nul(iface), nul(member), onSignal, nullptr,
                                  userdata) < 0) {
        delete userdata;
        return {};
    }
    sd_bus_slot_set_destroy_callback(slot, [](void* ud) { delete static_cast<Handler*>(ud); });
    return Subscription(slot);
}

Subscription Bus::onPropertiesChanged(const std::string& sender, const std::string& path,
                                      std::function<void(const Value&, const std::string&)> fn) {
    return subscribe(sender, path, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                     [fn = std::move(fn)](const Value& a, sd_bus_message*) {
                         if (a.size() >= 2 && a[1].is_object()) fn(a[1], a[0].get<std::string>());
                     });
}

int Bus::fd() const {
    return m_bus ? sd_bus_get_fd(m_bus) : -1;
}

short Bus::events() const {
    return m_bus ? static_cast<short>(sd_bus_get_events(m_bus)) : 0;
}

int Bus::timeoutMs() const {
    uint64_t usec = 0;
    if (!m_bus || sd_bus_get_timeout(m_bus, &usec) < 0 || usec == UINT64_MAX) return -1;
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t now = static_cast<uint64_t>(ts.tv_sec) * 1000000 + static_cast<uint64_t>(ts.tv_nsec) / 1000;
    return usec <= now ? 0 : static_cast<int>((usec - now + 999) / 1000);
}

void Bus::dispatch(short) {
    if (!m_bus) return;
    while (sd_bus_process(m_bus, nullptr) > 0) {
    }
}

} // namespace m3::dbus
