#pragma once

#include "core/Signal.h"

#include <ctime>
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
    // Screen recording with wf-recorder, as Settings → the screenshot
    // panel's Record tab sets it up (config /recorder/…): what (a region, a
    // window, a screen), the file format (MP4, WebM, MKV, GIF), resolution,
    // frame rate, quality, audio (system sound or the microphone) and
    // hardware encoding
    struct RecordOptions {
        int mode = 0;              // 0 a region, 1 a window, 2 a screen
        std::string format = "mp4"; // mp4, webm, mkv, gif
        int height = 0;            // 0: as captured; else scaled to it
        int fps = 60;
        std::string quality = "high"; // low, medium, high
        std::string audio = "off";    // off, system, mic
        bool hardware = false;        // VA-API (MP4 and MKV)
        static RecordOptions saved();
    };
    // `delayMs` first (a countdown), on `output` for a screen
    void startRecording(const RecordOptions& options, const std::string& output, int delayMs = 0);
    void stopRecording();
    // The keybind: stops one going, else starts with the saved options (a
    // region, or the whole output)
    void toggleRecording(bool region, const std::string& output);
    bool recording() const { return m_recording; }
    std::time_t recordingSince() const { return m_since; }
    std::vector<std::string> recent(size_t count) const; // newest first

    Signal<const std::string&> taken; // the saved file
    Signal<> recordingChanged;

private:
    Screenshot() = default;
    void shoot(const std::vector<std::string>& target);
    bool m_busy = false, m_recording = false;
    std::time_t m_since = 0;
};

} // namespace m3
