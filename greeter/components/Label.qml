import QtQuick
import "../core"

Text {
    font.family: Theme.fontMain
    font.pixelSize: 15 * Theme.s
    color: Theme.onSurface
    renderType: Text.NativeRendering
    elide: Text.ElideRight
}
