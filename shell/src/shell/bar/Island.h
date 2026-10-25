#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "shell/bar/Status.h"
#include "ui/Node.h"

namespace m3::shell {

// The dynamic island in the middle of the bar. At rest it is the clock (with
// a playing indicator); it grows to show media controls on hover, the latest
// notification for a few seconds, and a workspace switch. Click: dashboard.
// On a side bar it grows down the bar instead, its clock stacked.
class Island : public ui::Button {
public:
    explicit Island(OpenPopup open);
    ~Island() override;

    void onHoverWithin(bool hovered) override;

protected:
    SizeF doMeasure(SizeF available) override;
    void doArrange() override;
    bool clipsChildren() const override { return true; }
    bool doAnimating() const override { return Button::doAnimating() || m_width.running(); }
    void doPaint(Renderer& r) override;

private:
    enum class Mode { Clock, Media, Notice };
    void show(Mode mode);
    void build();
    void tick();
    void flash(const std::string& icon, const std::string& text, int ms);
    float contentLength(); // the content's size along the bar

    OpenPopup m_open;
    bool m_vertical = false;
    Mode m_mode = Mode::Clock;
    bool m_hovered = false;
    ui::Flex* m_content = nullptr;
    ui::Tween m_width{120, ui::Spring::spatialDefault()}; // along the bar
    std::string m_noticeIcon, m_noticeText;
    EventLoop::TimerId m_clock = 0, m_noticeTimer = 0;
    int m_lastWorkspace = 0;
    Connection m_media, m_notify, m_ws, m_recording;
};

} // namespace m3::shell
