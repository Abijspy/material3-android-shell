import QtQuick
import "../core"

Item {
    id: root
    
    // Allows customizing the accent color, defaults to primary color
    property color barColor: Appearance.m3colors.m3primary
    
    // Control running/visible state
    property bool running: true
    
    clip: true
    
    // Background track
    Rectangle {
        anchors.fill: parent
        color: root.barColor
        opacity: 0.2
        radius: height / 2
    }
    
    // Animated indicator
    Rectangle {
        id: indicator
        height: parent.height
        width: parent.width * 0.4
        color: root.barColor
        radius: height / 2
        opacity: root.running ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        
        x: -indicator.width // Initial state
        
        SequentialAnimation on x {
            loops: Animation.Infinite
            running: root.running && root.visible && root.width > 0
            
            NumberAnimation {
                from: -indicator.width
                to: root.width
                duration: 1200
                easing.type: Easing.InOutQuad
            }
        }
    }
}
