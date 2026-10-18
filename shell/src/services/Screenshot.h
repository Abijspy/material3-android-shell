#pragma once

#include "core/Signal.h"

#include <string>
#include <vector>

namespace m3 {

// Screenshots with grim: a region picked with slurp, the active window (from
// Hyprland) or a whole output. Saved as PNG, optionally copied to the
// clipboard and opened in an editor (swappy or satty).
class Screenshot {
public:
    enum class Mode { Region, Window, Screen };

    static Screenshot& get();

    // `output`: the monitor for Screen mode. Waits `delayMs` first (the
    // caller's popup has closed by then).
    void capture(Mode mode, int delayMs, const std::string& output);
    std::string directory() const;

    // Text in a picked region, recognised by tesseract, onto the clipboard
    void ocr();
    // Screen recording with wf-recorder: the first call starts (a picked
    // region, or the whole output), the next stops and saves
    void toggleRecording(bool region, const std::string& output);
    bool recording() const { return m_recording; }
    std::vector<std::string> recent(size_t count) const; // newest first

    Signal<const std::string&> taken; // the saved file
    Signal<> recordingChanged;

private:
    Screenshot() = default;
    void shoot(const std::vector<std::string>& target);
    bool m_busy = false, m_recording = false;
};

} // namespace m3
