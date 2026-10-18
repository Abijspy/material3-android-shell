pragma ComponentBehavior: Bound
import QtQuick
import "../core"
import "."

Rectangle {
    id: root

    property bool loading: true
    property double pullProgress: 0

    property color colBg: Appearance.colors.colPrimaryContainer
    property color colShape: Appearance.colors.colOnPrimaryContainer

    // Size, color
    property double implicitSize: 48 * Appearance.effectiveScale
    implicitWidth: implicitSize
    implicitHeight: implicitSize
    radius: Math.min(width, height) / 2
    color: colBg
    property double baseShapeSize: Math.round(root.implicitSize * (38 / 48))
    property double leapZoomSize: root.baseShapeSize * 1.2
    property double leapZoomProgress: 0

    // Shape
    property list<var> shapes: [
        MaterialShape.Shape.SoftBurst,
        MaterialShape.Shape.Cookie6Sided,
        MaterialShape.Shape.Cookie12Sided,
        MaterialShape.Shape.Pill,
        MaterialShape.Shape.Sunny,
        MaterialShape.Shape.Cookie4Sided,
        MaterialShape.Shape.Oval,
    ]
    property int shapeIndex: 0
    property double pullRotation: root.loading ? 0 : -(root.pullProgress * 360)
    property double continuousRotation: 0
    property double leapRotation: 0

    RotationAnimation on continuousRotation {
        running: root.loading
        duration: 12000
        easing.type: Easing.Linear
        loops: Animation.Infinite
        from: 0
        to: 360
    }
    Timer {
        interval: 800
        running: root.loading
        repeat: true
        onTriggered: leapAnimation.start()
    }
    ParallelAnimation {
        id: leapAnimation
        PropertyAction { target: root; property: "shapeIndex"; value: (root.shapeIndex + 1) % root.shapes.length }
        RotationAnimation {
            target: root
            direction: RotationAnimation.Shortest
            property: "leapRotation"
            to: (root.leapRotation + 90) % 360
            duration: 350
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: root
            property: "leapZoomProgress"
            from: 0
            to: 1
            duration: 750
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Appearance.animationCurves.emphasized
        }
    }

    MaterialShape {
        id: shape
        anchors.centerIn: parent
        rotation: root.pullRotation + root.continuousRotation + root.leapRotation
        shape: root.shapes[root.shapeIndex]
        implicitSize: root.baseShapeSize
        color: root.colShape
        animation: Appearance.animation.elementMoveFast.numberAnimation.createObject(this)
    }
}
