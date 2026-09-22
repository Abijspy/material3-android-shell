import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
Rectangle {
    id: root; signal close(); signal settings(); radius: 28; color: shell.glass; border.color: shell.outline
    property var apps: [["LibreWolf", "Browse the web", "librewolf", "◉"], ["Files", "Thunar file manager", "thunar", "▱"], ["Terminal", "foot", "foot", ">_"], ["Settings", "Aura preferences", "", "⚙"]]
    ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 16
        RowLayout { Layout.fillWidth: true; Label { text: "Apps"; font.pixelSize: 28; font.weight: Font.DemiBold; color: shell.text; Layout.fillWidth: true }; ToolButton { text: "×"; onClicked: root.close() } }
        TextField { id: query; Layout.fillWidth: true; placeholderText: "Search apps, files and actions"; font.pixelSize: 17; leftPadding: 16; background: Rectangle { radius: 18; color: shell.surfaceHigh } }
        ListView { Layout.fillWidth: true; Layout.fillHeight: true; model: root.apps.filter(a => a[0].toLowerCase().includes(query.text.toLowerCase())); spacing: 5
            delegate: Button { width: ListView.view.width; height: 66; onClicked: { if (modelData[2] !== "") shell.run(modelData[2]); else root.settings() }
                background: Rectangle { radius: 18; color: parent.hovered ? shell.primaryContainer : "transparent" }
                contentItem: RowLayout { spacing: 15; Label { text: modelData[3]; color: shell.primary; font.pixelSize: 26; Layout.preferredWidth: 30 }; ColumnLayout { spacing: 2; Label { text: modelData[0]; color: shell.text; font.pixelSize: 16 }; Label { text: modelData[1]; color: shell.muted; font.pixelSize: 13 } } }
            }
        }
        Label { text: "Enter to launch  ·  Esc to close"; color: shell.muted; font.pixelSize: 12 }
    }
}
