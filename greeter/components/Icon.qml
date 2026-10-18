import QtQuick
import "../core"

// Material Symbols Rounded glyph, as used across the shell.
Text {
    property real iconSize: 20 * Theme.s
    property real fill: 0

    font.family: Theme.fontIcon
    font.pixelSize: iconSize
    font.variableAxes: ({ "FILL": fill, "wght": 400, "opsz": 24 })
    color: Theme.onSurface
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    renderType: Text.NativeRendering
}
