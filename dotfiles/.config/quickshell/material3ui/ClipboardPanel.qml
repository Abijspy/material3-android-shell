import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell.Io
Rectangle { id: root; signal close(); radius: 28; color: shell.glass; border.color: shell.outline
    property var entries: []
    function reload() { entries = []; history.running = true }
    Component.onCompleted: reload()
    Process { id: history; command: ["sh", "-lc", "aura-system clipboard-list"]
        stdout: SplitParser { onRead: data => { if (data.length > 0) root.entries = root.entries.concat([data]) } }
    }
    ColumnLayout { anchors.fill: parent; anchors.margins: 22; RowLayout { Layout.fillWidth: true; Label { text: "Clipboard"; font.pixelSize: 24; color: shell.text; Layout.fillWidth: true }; ToolButton { text: "×"; onClicked: root.close() } }
        ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5; model: root.entries
            delegate: Button { width: ListView.view.width; height: 58; text: modelData; horizontalAlignment: Text.AlignLeft; elide: Text.ElideRight; onClicked: { shell.run("aura-system clipboard-copy " + JSON.stringify(modelData)); root.close() } }
        }
        RowLayout { Layout.fillWidth: true; Label { text: entries.length ? "cliphist history" : "Copy something to begin history"; color: shell.muted; font.pixelSize: 12; Layout.fillWidth: true }; ToolButton { text: "↻"; onClicked: root.reload() }; ToolButton { text: "Clear"; onClicked: { shell.run("aura-system clipboard-clear"); root.reload() } } }
    }
}
