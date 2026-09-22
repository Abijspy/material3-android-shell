import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root; signal close(); color: shell.surface
    property var sections: [["Connected devices", "Wi‑Fi, VPN, Ethernet and Bluetooth"], ["Wallpaper & style", "Wallpaper and Matugen palette"], ["Display & brightness", "Displays and brightness"], ["Sound & vibration", "PipeWire and channel balance"], ["Notifications", "Mako and Do Not Disturb"], ["Security & privacy", "Lock screen and PolicyKit"], ["Location & weather", "Location preference"], ["Bar settings", "Aura bar preference"], ["Apps", "Default applications"], ["Battery", "UPower and power profiles"], ["Accessibility", "Motion and contrast"], ["System", "Updates, device and accounts"]]
    function actions() {
        const a = {
          "Connected devices": [["Choose Wi‑Fi network", "NetworkManager connection editor", "aura-system wifi-menu"], ["Toggle Wi‑Fi", shell.wifi ? "Turn radio off" : "Turn radio on", "aura-system wifi " + (shell.wifi ? "off" : "on")], ["Manage Bluetooth", "Pair, trust and connect devices", "aura-system bluetooth-menu"], ["Network details", "Address, gateway and link state", "foot -e sh -lc 'aura-system network-info; read -r'"], ["VPN profiles", "NetworkManager profile list", "foot -e sh -lc 'nmcli connection show; read -r'"]],
          "Wallpaper & style": [["Choose wallpaper", "Apply with swww and create Matugen palette", "aura-system wallpaper-pick"], ["Reload colour palette", "Reload generated Material colours", "quickshell -c aura ipc call aura reloadColours"], ["Wallpaper library", "Open Pictures/Wallpapers", "thunar $HOME/Pictures/Wallpapers"]],
          "Display & brightness": [["Brightness", "Hardware backlight control", "foot -e brightnessctl"], ["Monitor layout", "Show connected Hyprland outputs", "foot -e sh -lc 'hyprctl monitors; read -r'"], ["Apply display config", "Reload hyprland.conf", "aura-system display-reload"]],
          "Sound & vibration": [["Audio controls", "Manage PipeWire streams and devices", "pavucontrol"], ["Mute output", "Toggle default sink", "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"], ["Reset channel balance", "Set both output channels equally", "aura-system balance 0"]],
          "Notifications": [["Toggle Do Not Disturb", shell.dnd ? "Return to normal notifications" : "Silence notifications", "aura-system dnd " + (shell.dnd ? "default" : "do-not-disturb")], ["Notification history", "Mako history", "foot -e sh -lc 'makoctl history; read -r'"], ["Dismiss notifications", "Clear visible notifications", "makoctl dismiss -a"]],
          "Security & privacy": [["Lock now", "Start Hyprlock", "aura-system lock"], ["PolicyKit status", "Show authentication agent", "foot -e sh -lc 'pgrep -af polkit; read -r'"]],
          "Location & weather": [["Set location", "Save a city for weather widgets", "foot -e sh -lc 'printf \"City: \"; read -r city; aura-system location \"$city\"'"], ["Saved location", "View configured city", "foot -e sh -lc 'cat ~/.config/aura/location 2>/dev/null || echo Not-set; read -r'"]],
          "Bar settings": [["Show bar", "Save visible preference", "aura-system bar-toggle true"], ["Hide bar", "Save hidden preference", "aura-system bar-toggle false"], ["Reload Aura", "Apply bar settings", "quickshell -c aura ipc call aura reload"]],
          "Apps": [["Default browser", "Set LibreWolf", "aura-system default-browser"], ["Default file manager", "Set Thunar", "aura-system default-files"], ["Installed apps", "Browse desktop entries", "thunar /usr/share/applications"]],
          "Battery": [["Battery status", shell.batteryInfo || "No battery", "foot -e sh -lc 'aura-system battery; read -r'"], ["Balanced", "Balanced power profile", "aura-system power-profile balanced"], ["Power saver", "Reduce energy use", "aura-system power-profile power-saver"], ["Performance", "Prioritise speed", "aura-system power-profile performance"]],
          "Accessibility": [["Reduce motion", "Disable Hyprland animations", "aura-system accessibility false"], ["Enable motion", "Restore animations", "aura-system accessibility true"], ["Qt appearance", "Open contrast and font tools", "qt6ct"]],
          "System": [["System update", "Run pacman upgrade", "aura-system update"], ["About device", shell.aboutInfo || "Loading…", "foot -e sh -lc 'aura-system about; read -r'"], ["Users & accounts", "List local desktop users", "aura-system users"]]
        }; return a[shell.page] || []
    }
    RowLayout { anchors.fill: parent; spacing: 0
      Rectangle { Layout.preferredWidth: 340; Layout.fillHeight: true; color: shell.surfaceContainer
        ColumnLayout { anchors.fill: parent; anchors.margins: 22; spacing: 14
          RowLayout { Layout.fillWidth: true; Label { text: "Settings"; font.pixelSize: 30; font.weight: Font.DemiBold; color: shell.text; Layout.fillWidth: true }; ToolButton { text: "×"; onClicked: root.close() } }
          TextField { id: search; Layout.fillWidth: true; placeholderText: "Search settings"; leftPadding: 14; background: Rectangle { radius: 18; color: shell.surfaceHigh } }
          ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 3; model: root.sections.filter(x => x[0].toLowerCase().includes(search.text.toLowerCase()))
            delegate: Button { width: ListView.view.width; height: 62; checkable: true; checked: shell.page === modelData[0]; onClicked: shell.page = modelData[0]; background: Rectangle { radius: 18; color: parent.checked ? shell.primaryContainer : "transparent" }; contentItem: Column { anchors.verticalCenter: parent.verticalCenter; Text { text: modelData[0]; color: shell.text }; Text { text: modelData[1]; color: shell.muted; font.pixelSize: 11; width: 275; elide: Text.ElideRight } } }
          }
        }
      }
      Flickable { Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: width; contentHeight: page.implicitHeight + 80; clip: true
        ColumnLayout { id: page; width: parent.width; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 42; spacing: 17
          Label { text: shell.page; color: shell.text; font.pixelSize: 34; font.weight: Font.DemiBold }
          Label { text: root.sections.find(x => x[0] === shell.page)[1]; color: shell.muted; font.pixelSize: 16 }
          Repeater { model: root.actions(); delegate: Button { Layout.fillWidth: true; implicitHeight: 80; onClicked: shell.run(modelData[2]); background: Rectangle { radius: 20; color: parent.hovered ? shell.surfaceHigh : shell.surfaceContainer; border.color: parent.hovered ? shell.primary : "transparent" }; contentItem: RowLayout { anchors.fill: parent; anchors.leftMargin: 19; anchors.rightMargin: 19; ColumnLayout { Layout.fillWidth: true; Label { text: modelData[0]; color: shell.text; font.pixelSize: 16; font.weight: Font.DemiBold }; Label { text: modelData[1]; color: shell.muted; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight } }; Label { text: "›"; color: shell.primary; font.pixelSize: 28 } } } }
          Rectangle { visible: shell.page === "Sound & vibration"; Layout.fillWidth: true; implicitHeight: 125; radius: 20; color: shell.surfaceContainer
            ColumnLayout { anchors.fill: parent; anchors.margins: 18; Label { text: "Volume balancer"; color: shell.text; font.pixelSize: 17; font.weight: Font.DemiBold }; Label { text: "Left and right channels are set through PipeWire PulseAudio. Release to apply."; color: shell.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }; Slider { Layout.fillWidth: true; from: -100; to: 100; value: shell.balance; onMoved: shell.balance = value; onPressedChanged: if (!pressed) shell.run("aura-system balance " + Math.round(value)) } }
          }
        }
      }
    }
}
