import QtQuick
import QtQuick.Effects
import "../core"
import "../core/functions" as Functions

RectangularShadow {
    required property var target
    anchors.fill: target
    radius: target.radius ?? 0
    blur: 0.9 * Appearance.sizes.elevationMargin
    offset: Qt.vector2d(0.0, 1.0)
    spread: 1
    color: Functions.ColorUtils.applyAlpha(Appearance.colors.colShadow, 0.2)
    cached: true
}
