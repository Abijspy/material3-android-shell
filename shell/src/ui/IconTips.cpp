// Plain words for an icon-only button with no tooltip of its own: what the
// Material Symbol it shows usually does. The toolkit shows them on hover
// (Surface), so every round icon button explains itself; a tooltip an app
// sets says more and wins.
#include "ui/IconTips.h"

#include <cctype>
#include <unordered_map>

namespace m3::ui {

std::string iconTip(const std::string& icon) {
    if (icon.empty() || icon.find('/') != std::string::npos) return ""; // an SVG file, not a symbol
    static const std::unordered_map<std::string, std::string> words = {
        // Moving about
        {"arrow_back", "Back"}, {"arrow_forward", "Forward"}, {"arrow_upward", "Up"}, {"arrow_downward", "Down"},
        {"chevron_left", "Previous"}, {"chevron_right", "Next"}, {"keyboard_arrow_up", "Move up"}, {"keyboard_arrow_down", "Move down"},
        {"keyboard_arrow_left", "Previous"}, {"keyboard_arrow_right", "Next"}, {"expand_more", "Show more"}, {"expand_less", "Show less"},
        {"first_page", "First page"}, {"last_page", "Last page"}, {"home", "Home"}, {"north_west", "Open"}, {"open_in_new", "Open"},
        {"open_in_full", "Expand"}, {"close_fullscreen", "Shrink"}, {"fullscreen", "Full screen"}, {"fullscreen_exit", "Leave full screen"},
        {"menu", "Menu"}, {"more_vert", "More options"}, {"more_horiz", "More options"}, {"apps", "Apps"},
        // Windows and panes
        {"close", "Close"}, {"cancel", "Cancel"}, {"crop_square", "Maximise"}, {"filter_none", "Restore"}, {"minimize", "Minimise"},
        {"settings", "Settings"}, {"tune", "Settings"}, {"view_sidebar", "Side panel"}, {"right_panel_open", "Details pane"},
        {"left_panel_open", "Side panel"}, {"vertical_split", "Split side by side"}, {"horizontal_split", "Split top and bottom"},
        {"splitscreen", "Two panes"}, {"grid_view", "Grid view"}, {"view_list", "List view"}, {"view_module", "Grid view"},
        {"list", "List view"}, {"dashboard", "Overview"}, {"tab", "New tab"}, {"add_box", "New tab"},
        // Making and changing
        {"add", "Add"}, {"remove", "Remove"}, {"edit", "Edit"}, {"edit_note", "Edit"}, {"delete", "Delete"},
        {"delete_forever", "Delete for good"}, {"delete_sweep", "Clear all"}, {"restore_from_trash", "Restore"}, {"save", "Save"},
        {"save_as", "Save as"}, {"download", "Download"}, {"upload", "Upload"}, {"undo", "Undo"}, {"redo", "Redo"},
        {"content_copy", "Copy"}, {"content_cut", "Cut"}, {"content_paste", "Paste"}, {"select_all", "Select all"},
        {"check", "Done"}, {"done", "Done"}, {"check_box", "Checklist"}, {"clear", "Clear"}, {"backspace", "Delete"},
        {"refresh", "Refresh"}, {"sync", "Sync"}, {"replay", "Start again"}, {"restart_alt", "Restart"}, {"reset_settings", "Reset"},
        {"create_new_folder", "New folder"}, {"note_add", "New file"}, {"post_add", "New"}, {"drive_file_rename_outline", "Rename"},
        {"label", "Labels"}, {"palette", "Colour"}, {"brush", "Edit picture"}, {"format_paint", "Format"},
        {"rotate_left", "Rotate left"}, {"rotate_right", "Rotate right"}, {"flip", "Flip"}, {"crop", "Crop"},
        {"zoom_in", "Zoom in"}, {"zoom_out", "Zoom out"}, {"fit_screen", "Fit"}, {"filter_list", "Filter"}, {"sort", "Sort"},
        {"sort_by_alpha", "Sort A to Z"}, {"swap_vert", "Swap"}, {"compress", "Compress"}, {"unarchive", "Extract"},
        {"archive", "Archive"}, {"keep", "Pin"}, {"keep_off", "Unpin"}, {"push_pin", "Pin"}, {"star", "Star"},
        {"favorite", "Favourite"}, {"favorite_border", "Favourite"}, {"bookmark", "Bookmark"}, {"bookmark_add", "Add a bookmark"},
        // Finding and seeing
        {"search", "Search"}, {"search_off", "Close search"}, {"find_replace", "Find and replace"}, {"match_case", "Match case"},
        {"visibility", "Show"}, {"visibility_off", "Hide"}, {"info", "Details"}, {"help", "Help"}, {"description", "Details"},
        {"history", "History"}, {"schedule", "Later"}, {"today", "Today"}, {"event", "Event"}, {"calendar_month", "Calendar"},
        {"language", "Website"}, {"link", "Copy link"}, {"share", "Share"}, {"send", "Send"}, {"print", "Print"},
        {"dark_mode", "Night mode"}, {"light_mode", "Light mode"}, {"contrast", "Contrast"}, {"translate", "Translate"},
        // Files and places
        {"folder", "Folder"}, {"folder_open", "Open the folder"}, {"drive_folder_upload", "Up a folder"}, {"terminal", "Terminal"},
        {"eject", "Eject"}, {"usb", "Drive"}, {"cloud", "Cloud"}, {"lan", "Network"}, {"image", "Picture"},
        {"add_photo_alternate", "Add a picture"}, {"photo_album", "Albums"}, {"wallpaper", "Set as wallpaper"},
        {"slideshow", "Slideshow"}, {"photo_camera", "Take a photo"}, {"cameraswitch", "Switch camera"},
        {"picture_as_pdf", "PDF"}, {"attach_file", "Attach"}, {"qr_code", "QR code"}, {"quick_share", "Quick Share"},
        // Playing
        {"play_arrow", "Play"}, {"pause", "Pause"}, {"stop", "Stop"}, {"skip_next", "Next"}, {"skip_previous", "Previous"},
        {"fast_forward", "Forward"}, {"fast_rewind", "Rewind"}, {"forward_10", "Forward 10 seconds"}, {"replay_10", "Back 10 seconds"},
        {"shuffle", "Shuffle"}, {"repeat", "Repeat"}, {"repeat_one", "Repeat one"}, {"queue_music", "Queue"},
        {"playlist_add", "Add to a playlist"}, {"lyrics", "Lyrics"}, {"subtitles", "Subtitles"}, {"closed_caption", "Subtitles"},
        {"graphic_eq", "Equaliser"}, {"speed", "Speed"}, {"volume_up", "Volume"}, {"volume_off", "Muted"}, {"volume_mute", "Mute"},
        {"mic", "Microphone"}, {"mic_off", "Microphone off"}, {"videocam", "Record video"}, {"fiber_manual_record", "Record"},
        // System
        {"power_settings_new", "Power"}, {"lock", "Lock"}, {"logout", "Log out"}, {"bedtime", "Sleep"}, {"wifi", "Wi-Fi"},
        {"bluetooth", "Bluetooth"}, {"notifications", "Notifications"}, {"notifications_off", "Do not disturb"},
        {"brightness_medium", "Brightness"}, {"battery_full", "Battery"}, {"system_update", "Updates"}, {"storefront", "Store"},
        {"function", "Functions"}, {"calculate", "Calculator"}, {"alarm", "Alarm"}, {"timer", "Timer"}, {"add_alert", "Remind me"},
        {"alarm_on", "Reminder"}, {"alarm_off", "Remove the reminder"}, {"public", "World clock"}, {"verified", "Verified"},
    };
    if (auto it = words.find(icon); it != words.end()) return it->second;
    // Unknown: its own name, readable ("drive_file_move" → "Drive file move")
    std::string out;
    for (char c : icon) out += c == '_' ? ' ' : c;
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

} // namespace m3::ui
