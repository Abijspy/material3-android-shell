#pragma once

#include "core/EventLoop.h"

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;

namespace m3::dbus {

// D-Bus values arrive as JSON: strings, numbers, bools, arrays, objects for
// a{sv} dictionaries and variants unwrapped. Byte arrays ("ay") become
// json::binary so pixmaps stay compact.
using Value = nlohmann::json;

Value readAll(sd_bus_message* m);

// Appends method arguments; use the helpers below or sd_bus_message_append.
using Writer = std::function<void(sd_bus_message*)>;

struct ObjectPath {
    std::string path;
};

namespace detail {
void append(sd_bus_message* m, const std::string& v);
void append(sd_bus_message* m, const char* v);
void append(sd_bus_message* m, bool v);
void append(sd_bus_message* m, int32_t v);
void append(sd_bus_message* m, uint32_t v);
void append(sd_bus_message* m, double v);
void append(sd_bus_message* m, const ObjectPath& v);
} // namespace detail

template <typename... Args>
Writer args(Args... a) {
    return [=](sd_bus_message* m) { (detail::append(m, a), ...); };
}

// A variant holding one basic value (for Properties.Set)
Writer variant(bool v);
Writer variant(const std::string& v);

// Unsubscribes when destroyed.
class Subscription {
public:
    Subscription() = default;
    explicit Subscription(sd_bus_slot* slot) : m_slot(slot) {}
    Subscription(Subscription&& o) noexcept : m_slot(o.m_slot) { o.m_slot = nullptr; }
    Subscription& operator=(Subscription&& o) noexcept;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription();

private:
    sd_bus_slot* m_slot = nullptr;
};

// One bus connection driven by the event loop. Every call is asynchronous;
// replies and signals run on the loop thread.
class Bus : public EventSource {
public:
    using Reply = std::function<void(const Value& args, const std::string& error)>;
    using Handler = std::function<void(const Value& args, sd_bus_message* msg)>;

    static Bus& system();
    static Bus& session();
    ~Bus() override;

    bool connected() const { return m_bus != nullptr; }
    sd_bus* raw() const { return m_bus; }
    std::string uniqueName() const;

    void call(const std::string& dest, const std::string& path, const std::string& iface, const std::string& method,
              const Writer& write = {}, Reply reply = {});

    // Properties.GetAll as one object; empty object on error
    void getAll(const std::string& dest, const std::string& path, const std::string& iface,
                std::function<void(const Value& props, bool ok)> done);
    void setProperty(const std::string& dest, const std::string& path, const std::string& iface,
                     const std::string& name, const Writer& value);

    // Empty strings match anything
    [[nodiscard]] Subscription subscribe(const std::string& sender, const std::string& path, const std::string& iface,
                                         const std::string& member, Handler handler);
    // PropertiesChanged on one object: (changed properties, interface)
    [[nodiscard]] Subscription onPropertiesChanged(const std::string& sender, const std::string& path,
                                                   std::function<void(const Value& changed, const std::string& iface)> fn);

    // EventSource
    int fd() const override;
    short events() const override;
    int timeoutMs() const override;
    void dispatch(short revents) override;

private:
    explicit Bus(bool system);

    sd_bus* m_bus = nullptr;
};

} // namespace m3::dbus
