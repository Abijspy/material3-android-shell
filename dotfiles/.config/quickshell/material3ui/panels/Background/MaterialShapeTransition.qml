import QtQuick
import Qt5Compat.GraphicalEffects
import "../../widgets" as Widgets

Item {
    id: root
    
    property real progress: 0.0
    property bool active: false
    property var sourceItem: null
    
    visible: active && progress < 1.0

    function pickRandomShape() {
        shapeMaskItem.pickRandomShape();
    }

    Item {
        id: shapeMaskContainer
        anchors.fill: parent
        visible: false
        layer.enabled: true

        Widgets.MaterialShape {
            id: shapeMaskItem
            anchors.centerIn: parent
            color: "black"
            property real maxDim: Math.sqrt(parent.width * parent.width + parent.height * parent.height)
            // Set a fixed large size. Changing implicitSize on a Canvas forces FBO reallocation and causes massive stuttering.
            implicitSize: maxDim
            // Animate the GPU scale instead. Use 4.0 multiplier because shapes like Triangle have edges very close to center and need massive scaling to cover screen corners.
            scale: root.progress * 4.0
            smooth: true
            // Disable the internal morph animation so it jumps to the new shape instantly without drawing FBO every frame for 350ms
            animation: NumberAnimation { duration: 0 }

            property var shapes: ["Circle", "Square", "Slanted", "Arch", "Fan", "Arrow", "SemiCircle", "Oval", "Pill", "Triangle", "Diamond", "ClamShell", "Pentagon", "Gem", "Sunny", "VerySunny", "Cookie4Sided", "Cookie6Sided", "Cookie7Sided", "Cookie9Sided", "Cookie12Sided", "Ghostish", "Clover4Leaf", "Clover8Leaf", "Burst", "SoftBurst", "Boom", "SoftBoom", "Flower", "Puffy", "PuffyDiamond", "PixelCircle", "PixelTriangle", "Bun", "Heart", "Squircle"]

            function pickRandomShape() {
                shapeString = shapes[Math.floor(Math.random() * shapes.length)]
            }
        }
    }

    OpacityMask {
        id: materialTransitionMask
        anchors.fill: parent
        source: root.sourceItem
        maskSource: shapeMaskContainer
    }
}
