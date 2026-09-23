import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: root; signal close(); radius: 30; color: shell.glass; border.color: shell.outline
    component Tile: Button { property string icon: ""; property string title: ""; property string subtitle: ""; implicitHeight: 82; Layout.fillWidth: true
        background: Rectangle { radius: 20; color: parent.checked ? shell.primaryContainer : shell.surfaceHigh }
        contentItem: RowLayout { spacing: 12; Label { text: parent.icon; font.pixelSize: 25; color: parent.checked ? shell.primary : shell.text }; ColumnLayout { Layout.fillWidth: true; Label { text: parent.title; color: shell.text; font.weight: Font.DemiBold }; Label { text: parent.subtitle; color: shell.muted; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true } }; Label { text: "›"; color: shell.muted; font.pixelSize: 22 } }
    }
    ColumnLayout { anchors.fill: parent; anchors.margins: 22; spacing: 13
        RowLayout { Layout.fillWidth: true; Label { text: "Control centre"; color: shell.text; font.pixelSize: 26; font.weight: Font.DemiBold; Layout.fillWidth: true }; ToolButton { text: "⚙"; onClicked: { root.close(); shell.settingsOpen = true } }; ToolButton { text: "×"; onClicked: root.close() } }
        RowLayout { Layout.fillWidth: true; Tile { Layout.fillWidth: true; icon: "⌁"; title: "Wi‑Fi"; subtitle: shell.wifi ? "NetworkManager enabled" : "Off"; checkable: true; checked: shell.wifi; onClicked: { shell.wifi = checked; shell.run("material3ui-system wifi " + (checked ? "on" : "off")) } }; Tile { Layout.fillWidth: true; icon: "ᛒ"; title: "Bluetooth"; subtitle: shell.bluetooth ? "Adapter powered" : "Off"; checkable: true; checked: shell.bluetooth; onClicked: { shell.bluetooth = checked; shell.run("material3ui-system bluetooth " + (checked ? "on" : "off")) } } }
        RowLayout { Layout.fillWidth: true; Tile { Layout.fillWidth: true; icon: "◉"; title: "Focus"; subtitle: shell.dnd ? "Do not disturb" : "All interruptions"; checkable: true; checked: shell.dnd; onClicked: shell.dnd = checked }; Tile { Layout.fillWidth: true; icon: "☼"; title: "Brightness"; subtitle: shell.brightness + "%"; onClicked: {} } }
        Rectangle { Layout.fillWidth: true; height: 96; radius: 20; color: shell.surfaceHigh
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; Label { text: "Volume  " + shell.volume + "%"; color: shell.text }; Slider { Layout.fillWidth: true; from: 0; to: 100; value: shell.volume; onMoved: shell.volume = value }
                RowLayout { Layout.fillWidth: true; Label { text: "L"; color: shell.muted }; Slider { Layout.fillWidth: true; from: -100; to: 100; value: shell.balance; onMoved: shell.balance = value }; Label { text: "R"; color: shell.muted }; Label { text: "Balance"; color: shell.muted; font.pixelSize: 12 } }
            }
        }
        RowLayout { Layout.fillWidth: true; Button { text: "▣  Screenshot"; Layout.fillWidth: true; onClicked: shell.run("material3ui-system screenshot") }; Button { text: "●  Record"; Layout.fillWidth: true; onClicked: shell.run("material3ui-system record") } }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 20; color: shell.surfaceHigh
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; RowLayout { Layout.fillWidth: true; Label { text: "Background apps"; color: shell.text; font.weight: Font.DemiBold; Layout.fillWidth: true }; Label { text: "Manage"; color: shell.primary } }; Label { text: "NetworkManager  ·  WirePlumber  ·  Clipboard history"; color: shell.muted; wrapMode: Text.Wrap; Layout.fillWidth: true } }
        }
        RowLayout { Layout.fillWidth: true; Label { text: "A  Abishek"; color: shell.text; font.weight: Font.DemiBold; Layout.fillWidth: true }; Label { text: shell.batteryInfo || "No battery"; color: shell.muted; elide: Text.ElideRight; Layout.maximumWidth: 180 }; ToolButton { text: "⏻"; onClicked: { root.close(); shell.powerOpen = true } } }
    }
}
