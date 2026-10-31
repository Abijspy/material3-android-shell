#pragma once

#include "core/Signal.h"
#include "services/TimeZones.h"
#include "ui/Node.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace m3::ui {
class Flex;
class Label;
} // namespace m3::ui

namespace m3::shell {

class Popup;

// The world, flat: land, lakes and country borders, the night side shaded
// where the sun has set, and a dot for each of tzdata's places. The pointer
// names the place nearest it and a click picks that one. The chosen place
// has a pin and its name; its UTC offset is a band down the map, and every
// place on the same time now has its dot lit.
class TimeZoneMap : public ui::Node {
public:
    explicit TimeZoneMap(const std::vector<tz::Zone>& zones);
    ~TimeZoneMap() override { *m_alive = false; }
    // False once the map is gone: for callbacks that come back later
    std::shared_ptr<const bool> alive() const { return m_alive; }

    // The pin and band for zone `id` (any of tzdata's: one without a place
    // has just its band)
    void select(const std::string& id);
    std::function<void(const tz::Zone&)> onPicked;

    bool interactive() const override { return true; }
    void onHover(bool on) override;
    void onPointerMove(float x, float y) override;
    void onPress(uint32_t button, float x, float y) override;

protected:
    SizeF doMeasure(SizeF available) override;
    void doArrange() override;
    void doPaint(Renderer& r) override;

private:
    // Map ↔ node coordinates (plate carrée, the map file's latitudes)
    float toX(double lon) const;
    float toY(double lat) const;
    int placeAt(float x, float y) const;
    void hoverOn(int index);

    std::vector<tz::Zone> m_zones;               // those inside the map's latitudes
    std::vector<std::optional<int>> m_offsets;   // each one's UTC offset now, in minutes
    int m_selected = -1, m_hover = -1;
    std::optional<int> m_offsetMinutes; // the chosen zone's, now
    ui::Flex *m_pinChip, *m_hoverChip;
    ui::Label *m_pinLabel, *m_hoverLabel;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

// The system's time zone was set from here (or setting it failed): rows
// showing it read it again
Signal<>& timeZoneChanged();

// Settings → System's time zone: the zone in words (its place, UTC offset
// and time there) and Change, which opens the map
void addTimeZoneRow(ui::Flex* group);

// The "timezone" popup: the map, a search for any of tzdata's zones, the
// chosen one in detail (its abbreviation, the time there, its next change
// of clocks), and Set, which sets it with timedatectl
void timeZoneDialog(ui::Flex* card, Popup& popup);

} // namespace m3::shell
