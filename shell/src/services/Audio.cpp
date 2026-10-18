#include "services/Audio.h"

#include "core/EventLoop.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <pulse/pulseaudio.h>

namespace m3 {

namespace {

constexpr float kMaxVolume = 1.5f; // allow some boost, like the volume keys

pa_volume_t toPa(float v) {
    return static_cast<pa_volume_t>(std::lround(std::clamp(v, 0.0f, kMaxVolume) * PA_VOLUME_NORM));
}

} // namespace

float Audio::Device::volume() const {
    return volumes.empty() ? 0 : *std::max_element(volumes.begin(), volumes.end());
}

float Audio::Device::balance() const {
    if (volumes.size() != 2) return 0;
    const float l = volumes[0], r = volumes[1];
    if (l <= 0 && r <= 0) return 0;
    return l >= r ? -(1 - r / l) : 1 - l / r;
}

// Lives on the libpulse thread except where noted. Writes take the mainloop
// lock from the event-loop thread.
struct Audio::Impl {
    pa_threaded_mainloop* loop = nullptr;
    pa_context* ctx = nullptr;

    // pulse thread state, rebuilt on each refresh
    std::string defaultSink, defaultSource;
    std::vector<Device> sinks, sources;
    std::map<std::string, pa_channel_map> maps;
    std::map<std::string, pa_cvolume> cvolumes;
    int pending = 0;
    bool refreshQueued = false;

    // Copies the pulse thread's view for the event loop (under the lock)
    std::map<std::string, pa_channel_map> mapsShared;
    std::map<std::string, pa_cvolume> cvolumesShared;

    static Device toDevice(const char* name, const char* desc, const pa_cvolume& cv, const pa_channel_map& map, int mute) {
        Device d;
        d.name = name;
        d.description = desc ? desc : name;
        for (uint8_t i = 0; i < cv.channels; ++i) {
            d.volumes.push_back(static_cast<float>(cv.values[i]) / PA_VOLUME_NORM);
            d.channels.push_back(pa_channel_position_to_pretty_string(map.map[i]));
        }
        d.muted = mute != 0;
        return d;
    }

    void refresh() {
        if (pending > 0) {
            refreshQueued = true;
            return;
        }
        sinks.clear();
        sources.clear();
        pending = 3;
        pa_operation_unref(pa_context_get_server_info(ctx, &Impl::onServer, this));
        pa_operation_unref(pa_context_get_sink_info_list(ctx, &Impl::onSink, this));
        pa_operation_unref(pa_context_get_source_info_list(ctx, &Impl::onSource, this));
    }

    void finishOne() {
        if (--pending > 0) return;
        for (auto& d : sinks) d.isDefault = d.name == defaultSink;
        for (auto& d : sources) d.isDefault = d.name == defaultSource;
        mapsShared = maps;
        cvolumesShared = cvolumes;
        EventLoop::get().post([sinks = sinks, sources = sources] {
            auto& a = Audio::get();
            a.m_sinks = sinks;
            a.m_sources = sources;
            a.m_ready = true;
            a.changed.emit();
        });
        if (refreshQueued) {
            refreshQueued = false;
            refresh();
        }
    }

    static void onServer(pa_context*, const pa_server_info* info, void* ud) {
        auto* self = static_cast<Impl*>(ud);
        if (info) {
            self->defaultSink = info->default_sink_name ? info->default_sink_name : "";
            self->defaultSource = info->default_source_name ? info->default_source_name : "";
        }
        self->finishOne();
    }

    static void onSink(pa_context*, const pa_sink_info* info, int eol, void* ud) {
        auto* self = static_cast<Impl*>(ud);
        if (eol) return self->finishOne();
        self->sinks.push_back(toDevice(info->name, info->description, info->volume, info->channel_map, info->mute));
        self->maps[info->name] = info->channel_map;
        self->cvolumes[info->name] = info->volume;
    }

    static void onSource(pa_context*, const pa_source_info* info, int eol, void* ud) {
        auto* self = static_cast<Impl*>(ud);
        if (eol) return self->finishOne();
        // Monitors of outputs are not microphones
        if (info->monitor_of_sink != PA_INVALID_INDEX) return;
        self->sources.push_back(toDevice(info->name, info->description, info->volume, info->channel_map, info->mute));
        self->maps[info->name] = info->channel_map;
        self->cvolumes[info->name] = info->volume;
    }

    static void onEvent(pa_context*, pa_subscription_event_type_t, uint32_t, void* ud) {
        static_cast<Impl*>(ud)->refresh();
    }

    static void onState(pa_context* c, void* ud) {
        auto* self = static_cast<Impl*>(ud);
        switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            pa_context_set_subscribe_callback(c, &Impl::onEvent, self);
            pa_operation_unref(pa_context_subscribe(
                c, static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
                                                       PA_SUBSCRIPTION_MASK_SERVER),
                nullptr, nullptr));
            self->refresh();
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            EventLoop::get().post([] {
                Audio::get().m_ready = false;
                Audio::get().changed.emit();
            });
            break;
        default:
            break;
        }
    }

    // Event-loop side: run `fn(ctx)` under the mainloop lock
    template <typename F>
    void locked(F fn) {
        if (!loop) return;
        pa_threaded_mainloop_lock(loop);
        if (pa_context_get_state(ctx) == PA_CONTEXT_READY) fn(ctx);
        pa_threaded_mainloop_unlock(loop);
    }
};

Audio& Audio::get() {
    static Audio instance;
    return instance;
}

Audio::Audio() : m_impl(std::make_shared<Impl>()) {
    auto& i = *m_impl;
    i.loop = pa_threaded_mainloop_new();
    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "Material3UI");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "dev.material3ui.shell");
    i.ctx = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(i.loop), "Material3UI", props);
    pa_proplist_free(props);
    pa_context_set_state_callback(i.ctx, &Impl::onState, &i);
    // NOFAIL: keep retrying if the server starts after the shell
    pa_context_connect(i.ctx, nullptr, PA_CONTEXT_NOFAIL, nullptr);
    pa_threaded_mainloop_start(i.loop);
}

Audio::~Audio() {
    auto& i = *m_impl;
    if (!i.loop) return;
    pa_threaded_mainloop_stop(i.loop);
    pa_context_disconnect(i.ctx);
    pa_context_unref(i.ctx);
    pa_threaded_mainloop_free(i.loop);
    i.loop = nullptr;
}

const Audio::Device* Audio::sink() const {
    auto it = std::ranges::find_if(m_sinks, &Device::isDefault);
    return it != m_sinks.end() ? &*it : (m_sinks.empty() ? nullptr : &m_sinks.front());
}

const Audio::Device* Audio::source() const {
    auto it = std::ranges::find_if(m_sources, &Device::isDefault);
    return it != m_sources.end() ? &*it : (m_sources.empty() ? nullptr : &m_sources.front());
}

void Audio::setVolume(float v, bool input) {
    const Device* d = input ? source() : sink();
    if (!d) return;
    const std::string name = d->name;
    m_impl->locked([&](pa_context* c) {
        auto it = m_impl->cvolumesShared.find(name);
        if (it == m_impl->cvolumesShared.end()) return;
        pa_cvolume cv = it->second;
        // Scales every channel so the loudest lands on v: balance survives
        pa_cvolume_scale(&cv, toPa(v));
        pa_operation_unref(input ? pa_context_set_source_volume_by_name(c, name.c_str(), &cv, nullptr, nullptr)
                                 : pa_context_set_sink_volume_by_name(c, name.c_str(), &cv, nullptr, nullptr));
    });
}

void Audio::stepVolume(float delta) {
    if (const Device* d = sink()) {
        if (d->muted && delta > 0) setMuted(false);
        setVolume(std::clamp(d->volume() + delta, 0.0f, 1.0f));
    }
}

void Audio::setBalance(float b) {
    const Device* d = sink();
    if (!d) return;
    const std::string name = d->name;
    m_impl->locked([&](pa_context* c) {
        auto cv = m_impl->cvolumesShared.find(name);
        auto map = m_impl->mapsShared.find(name);
        if (cv == m_impl->cvolumesShared.end() || map == m_impl->mapsShared.end()) return;
        pa_cvolume v = cv->second;
        if (!pa_channel_map_can_balance(&map->second)) return;
        pa_cvolume_set_balance(&v, &map->second, std::clamp(b, -1.0f, 1.0f));
        pa_operation_unref(pa_context_set_sink_volume_by_name(c, name.c_str(), &v, nullptr, nullptr));
    });
}

void Audio::setChannelVolume(size_t channel, float value) {
    const Device* d = sink();
    if (!d) return;
    const std::string name = d->name;
    m_impl->locked([&](pa_context* c) {
        auto it = m_impl->cvolumesShared.find(name);
        if (it == m_impl->cvolumesShared.end() || channel >= it->second.channels) return;
        pa_cvolume cv = it->second;
        cv.values[channel] = toPa(value);
        pa_operation_unref(pa_context_set_sink_volume_by_name(c, name.c_str(), &cv, nullptr, nullptr));
    });
}

void Audio::setMuted(bool m, bool input) {
    const Device* d = input ? source() : sink();
    if (!d) return;
    const std::string name = d->name;
    m_impl->locked([&](pa_context* c) {
        pa_operation_unref(input ? pa_context_set_source_mute_by_name(c, name.c_str(), m, nullptr, nullptr)
                                 : pa_context_set_sink_mute_by_name(c, name.c_str(), m, nullptr, nullptr));
    });
}

void Audio::toggleMuted(bool input) {
    const Device* d = input ? source() : sink();
    if (d) setMuted(!d->muted, input);
}

void Audio::setDefault(const std::string& name, bool input) {
    m_impl->locked([&](pa_context* c) {
        pa_operation_unref(input ? pa_context_set_default_source(c, name.c_str(), nullptr, nullptr)
                                 : pa_context_set_default_sink(c, name.c_str(), nullptr, nullptr));
    });
}

} // namespace m3
