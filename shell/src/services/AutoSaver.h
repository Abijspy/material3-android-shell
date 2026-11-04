#pragma once

namespace m3 {

// Power saver at low battery (Settings → Battery): on battery and at or
// below /battery/autoSaverAt % (default 20), the power-saver profile; on
// the charger again, the profile from before (unless changed meanwhile).
// What it changed is kept in config.json, so a restart in between still
// puts it back. Off with /battery/autoSaver false
class AutoSaver {
public:
    static AutoSaver& get();
    void start();
    static bool enabled();
    static int threshold();

private:
    AutoSaver() = default;
    void check();
};

} // namespace m3
