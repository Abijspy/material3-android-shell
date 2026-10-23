#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "ui/Animation.h"
#include "ui/Node.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace m3::ui {
class Marquee;
}

// Building blocks shared by the popup panels
namespace m3::shell::kit {

// A column that re-runs `sync` whenever a watched service changes, and drops
// the subscriptions with the popup.
class Live : public ui::Flex {
public:
    Live();
    ~Live() override;
    std::vector<Connection> conns;
    std::function<void()> sync;
    // Service changes sync on the next loop iteration, coalesced: a sync may
    // rebuild the very button whose click caused the change
    void watch(Signal<>& s);
    void syncSoon();

private:
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    bool m_pending = false;
};

ui::Flex* header(ui::Node* parent, const std::string& icon, const std::string& title);
ui::Label* caption(ui::Node* parent, const std::string& text);

// A level bar as wide as it is given (battery charge)
class Meter : public ui::Node {
public:
    Meter(float value, ColorRef color, float height = 12);

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, m_height}; }
    void doPaint(Renderer& r) override;

private:
    float m_value, m_height;
    ColorRef m_color;
};

// Children left to right, wrapping onto new lines (chips)
class Flow : public ui::Node {
public:
    float spacing = 6;

protected:
    SizeF doMeasure(SizeF available) override { return place(available.w, false); }
    void doArrange() override { place(rect().w, true); }

private:
    SizeF place(float width, bool arrange) {
        float x = 0, y = 0, line = 0, widest = 0;
        for (auto& c : children()) {
            if (!c->shown()) continue;
            const SizeF s = c->measure({width, 1e6f});
            if (x > 0 && x + s.w > width) {
                x = 0;
                y += line + spacing;
                line = 0;
            }
            if (arrange) c->arrange({rect().x + x, rect().y + y, s.w, s.h});
            x += s.w + spacing;
            line = std::max(line, s.h);
            widest = std::max(widest, x - spacing);
        }
        return {widest, y + line};
    }
};

// M3 Expressive loading indicators, for work of unknown length. Both animate
// for as long as they are shown.
// Circular: a wavy arc that spins while it grows and shrinks.
class WavyLoader : public ui::Node {
public:
    explicit WavyLoader(float size = 36) : m_size(size) {}

protected:
    SizeF doMeasure(SizeF) override { return {m_size, m_size}; }
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return true; }

private:
    float m_size;
};

// Linear: a wavy stretch that sweeps along a flat track.
class WavyBar : public ui::Node {
protected:
    SizeF doMeasure(SizeF available) override { return {available.w, 16}; }
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return true; }
};

// The active player's progress as a ring around its children (an icon):
// the played arc waves while playing and lies flat when paused.
class WavyRing : public ui::Flex {
public:
    explicit WavyRing(float size);

protected:
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return Flex::doAnimating() || m_playing || m_amp.running(); }

private:
    float m_size;
    bool m_playing = false;
    ui::Tween m_amp{0, ui::motion::mediumMs, ui::Easing::standard()};
};

// M3 Expressive wavy progress: the played part a wave that moves while
// playing and lies flat when paused, the rest a straight track with a stop
// dot. Click or drag to seek.
class WavyProgress : public ui::Node {
public:
    void set(float value, bool playing);
    std::function<void(float)> onSeek; // released at this fraction

    bool interactive() const override { return static_cast<bool>(onSeek); }
    void onPress(uint32_t button, float x, float y) override;
    void onDrag(float x, float y) override;
    void onRelease() override;

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, 24}; }
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return m_playing || m_amp.running(); }

private:
    float m_value = 0, m_drag = -1;
    bool m_playing = false;
    ui::Tween m_amp{0, ui::motion::mediumMs, ui::Easing::standard()}; // wave height: 0 flat
};

// The media player, as Android 17's: the album art fills the card under a
// tint; the title and artist, wavy progress with times, and the controls,
// whose play button is a circle when paused and a rounded square while
// playing. Follows the active MPRIS player by itself.
class MediaCard : public ui::Flex {
public:
    // Compact (Quick Settings): the text beside the controls, half the height
    explicit MediaCard(bool compact = false);
    ~MediaCard() override;

protected:
    void doPaint(Renderer& r) override;
    // The title and artist take the card's width as it is, scrolling
    // (the title) or cut (the artist) within it
    void doArrange() override;

private:
    void sync();
    ui::Label *m_app, *m_artist, *m_elapsed, *m_left;
    ui::Marquee* m_title;
    ui::Flex* m_progressBox;
    WavyProgress* m_progress;
    ui::Button *m_play, *m_prev, *m_next;
    ui::Icon* m_playIcon;
    std::string m_art; // the cover file, "" for none
    Connection m_conn;
    std::shared_ptr<void> m_lease;
    EventLoop::TimerId m_tick = 0; // the times move on each second
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true); // for the cover's decode
    bool m_compact = false;
    float m_room = 0; // the text's width, as last arranged
};

// A user's picture, round (~/.face, as login screens use); without one,
// their initial on a tonal circle
class AvatarView : public ui::Flex {
public:
    AvatarView(std::string picture, const std::string& name, float size);
    ~AvatarView() override;

protected:
    void doPaint(Renderer& r) override;

private:
    std::string m_picture; // "" when there is none
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

// Round icon button (close, back, power…)
ui::Button* iconButton(ui::Node* parent, const std::string& icon, std::function<void()> onClick, float size = 20);
// An icon button for something that can't be undone (forget a network or
// a device): the first tap arms it, red, and a second within a few seconds
// acts. `key` names its target, so the armed state outlives the list
// rebuilding under it.
ui::Button* confirmButton(ui::Node* parent, const std::string& key, const std::string& icon, std::function<void()> onConfirm);

// Quick Settings tile, as Android 17 draws them: a pill with the icon in a
// chip of its own. Tiles with details (Wi-Fi, Bluetooth) light the chip when
// on, which then toggles while the rest opens the details; the others fill
// completely. The chip morphs from a circle (off) to a rounded square (on).
class Tile : public ui::Button {
public:
    Tile(const std::string& icon, const std::string& title);
    void set(bool active, const std::string& status, const std::string& icon = {});
    std::function<void()> onToggle;  // the chip, or the whole tile without details
    std::function<void()> onDetails; // the body, when set
    void setSelected(bool selected); // the editor's outline

private:
    ui::Button* m_chip;
    ui::Icon* m_icon;
    ui::Label* m_title;
    ui::Label* m_status;
    bool m_active = false;
};

// Small Quick Settings tile: the icon alone in a pill, filled when on
class SmallTile : public ui::Button {
public:
    SmallTile(const std::string& icon, float width);
    void set(bool active, const std::string& icon = {});
    void setSelected(bool selected);

private:
    ui::Icon* m_icon;
};

} // namespace m3::shell::kit
