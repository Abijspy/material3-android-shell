import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root; signal close(); color: shell.glass
    property var sections: [["Connected devices", "Wi‑Fi, VPN, Ethernet and Bluetooth"], ["Wallpaper & style", "Wallpaper and Matugen palette"], ["Display & brightness", "Displays and brightness"], ["Sound & vibration", "PipeWire and channel balance"], ["Notifications", "Mako and Do Not Disturb"], ["Security & privacy", "Lock screen and PolicyKit"], ["Location & weather", "Location preference"], ["Bar settings", "Material3UI bar preference"], ["Apps", "Default applications"], ["Battery", "UPower and power profiles"], ["Accessibility", "Motion and contrast"], ["System", "Updates, device and accounts"]]
    function actions() {
        const a = {
          "Connected devices": [["Choose Wi‑Fi network", "NetworkManager connection editor", "material3ui-system wifi-menu"], ["Toggle Wi‑Fi", shell.wifi ? "Turn radio off" : "Turn radio on", "material3ui-system wifi " + (shell.wifi ? "off" : "on")], ["Manage Bluetooth", "Pair, trust and connect devices", "material3ui-system bluetooth-menu"], ["Network details", "Address, gateway and link state", "foot -e sh -lc 'material3ui-system network-info; read -r'"], ["VPN profiles", "NetworkManager profile list", "foot -e sh -lc 'nmcli connection show; read -r'"]],
          "Wallpaper & style": [["Choose wallpaper", "Apply with swww and create Matugen palette", "material3ui-system wallpaper-pick"], ["Reload colour palette", "Reload generated Material colours", "quickshell -c material3ui ipc call material3ui reloadColours"], ["Wallpaper library", "Open Pictures/Wallpapers", "thunar $HOME/Pictures/Wallpapers"]],
          "Display & brightness": [["Brightness", "Hardware backlight control", "foot -e brightnessctl"], ["Monitor layout", "Show connected Hyprland outputs", "foot -e sh -lc 'hyprctl monitors; read -r'"], ["Apply display config", "Reload hyprland.conf", "material3ui-system display-reload"]],
          "Sound & vibration": [["Audio controls", "Manage PipeWire streams and devices", "pavucontrol"], ["Mute output", "Toggle default sink", "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"], ["Reset channel balance", "Set both output channels equally", "material3ui-system balance 0"]],
          "Notifications": [["Toggle Do Not Disturb", shell.dnd ? "Return to normal notifications" : "Silence notifications", "material3ui-system dnd " + (shell.dnd ? "default" : "do-not-disturb")], ["Notification history", "Mako history", "foot -e sh -lc 'makoctl history; read -r'"], ["Dismiss notifications", "Clear visible notifications", "makoctl dismiss -a"]],
          "Security & privacy": [["Lock now", "Start the Material3UI Hyprlock screen", "material3ui-system lock"], ["Authentication agent", "Check the built-in Hyprland Polkit agent", "material3ui-system polkit-status"]],
          "Location & weather": [["Set location", "Save a city for weather widgets", "foot -e sh -lc 'printf \"City: \"; read -r city; material3ui-system location \"$city\"'"], ["Saved location", "View configured city", "foot -e sh -lc 'cat ~/.config/material3ui/location 2>/dev/null || echo Not-set; read -r'"]],
          "Bar settings": [["Show bar", "Save visible preference", "material3ui-system bar-toggle true"], ["Hide bar", "Save hidden preference", "material3ui-system bar-toggle false"], ["Reload Material3UI", "Apply bar settings", "quickshell -c material3ui ipc call material3ui reload"]],
          "Apps": [["Default browser", "Set LibreWolf", "material3ui-system default-browser"], ["Default file manager", "Set Thunar", "material3ui-system default-files"], ["Installed apps", "Browse desktop entries", "thunar /usr/share/applications"]],
          "Battery": [["Battery status", shell.batteryInfo || "No battery", "foot -e sh -lc 'material3ui-system battery; read -r'"], ["Balanced", "Balanced power profile", "material3ui-system power-profile balanced"], ["Power saver", "Reduce energy use", "material3ui-system power-profile power-saver"], ["Performance", "Prioritise speed", "material3ui-system power-profile performance"]],
          "Accessibility": [["Reduce motion", "Disable Hyprland animations", "material3ui-system accessibility false"], ["Enable motion", "Restore animations", "material3ui-system accessibility true"], ["Qt appearance", "Open contrast and font tools", "qt6ct"]],
          "System": [["HyprMod", "Graphical Hyprland keybinds, rules, monitors and profiles", "material3ui-system hyprmod"], ["System update", "Run pacman upgrade", "material3ui-system update"], ["About device", shell.aboutInfo || "Loading…", "foot -e sh -lc 'material3ui-system about; read -r'"], ["Users & accounts", "List local desktop users", "material3ui-system users"]]
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
          BarEditor { }
          Rectangle { visible: shell.page === "Sound & vibration"; Layout.fillWidth: true; implicitHeight: 125; radius: 20; color: shell.surfaceContainer
            ColumnLayout { anchors.fill: parent; anchors.margins: 18; Label { text: "Volume balancer"; color: shell.text; font.pixelSize: 17; font.weight: Font.DemiBold }; Label { text: "Left and right channels are set through PipeWire PulseAudio. Release to apply."; color: shell.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }; Slider { Layout.fillWidth: true; from: -100; to: 100; value: shell.balance; onMoved: shell.balance = value; onPressedChanged: if (!pressed) shell.run("material3ui-system balance " + Math.round(value)) } }
          }
        }
      }
    }
}
