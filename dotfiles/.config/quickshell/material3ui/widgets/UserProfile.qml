import "../core"
import "../core/functions" as Functions
import "../services"
import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

/**
 * Universal User Profile widget for sidebars.
 * Shows Avatar, Display Name (or real name), Hostname, and Distribution/Uptime info.
 */
Rectangle {
    id: root
    implicitWidth: parent.width
    implicitHeight: 56 * Appearance.effectiveScale
    color: "transparent"
    
    property bool compact: false
    signal clicked()

    // Circular Avatar
    Item {
        id: avatarContainer
        width: 44 * Appearance.effectiveScale; height: 44 * Appearance.effectiveScale
        anchors.verticalCenter: parent.verticalCenter
        // Center of the icons above is at x=40 (12px margin + 56px/2). 
        // To align, avatar center must be 40. 40 - (44/2) = 18.
        x: 18 * Appearance.effectiveScale
        
        Image {
            id: avatarImage
            anchors.fill: parent
            source: {
                const profPath = Config.options.profile?.avatarPicture;
                if (profPath && profPath !== "") return `file://${profPath}`;
                const cfgPath = Config.options.bar?.avatar_path;
                if (cfgPath && cfgPath !== "") return `file://${cfgPath}`;
                if (SystemInfo.userAvatarValid) return "file://" + SystemInfo.userAvatarPath;
                return "";
            }
            sourceSize: Qt.size(width, height)
            fillMode: Image.PreserveAspectCrop
            visible: false
        }

        Rectangle {
            id: maskRect
            anchors.fill: parent
            radius: width / 2
            visible: false
        }

        OpacityMask {
            anchors.fill: parent
            source: avatarImage
            maskSource: maskRect
            visible: avatarImage.status === Image.Ready
        }
        
        MaterialSymbol {
            anchors.centerIn: parent
            visible: avatarImage.status !== Image.Ready
            text: "person"
            iconSize: 22 * Appearance.effectiveScale
            color: Appearance.m3colors.m3onPrimaryContainer
        }
    }
    
    // Text Column
    ColumnLayout {
        anchors.left: avatarContainer.right
        anchors.leftMargin: 14 * Appearance.effectiveScale
        anchors.right: parent.right
        anchors.rightMargin: 12 * Appearance.effectiveScale
        anchors.verticalCenter: parent.verticalCenter
        spacing: -2 * Appearance.effectiveScale
        
        opacity: root.compact ? 0.0 : 1.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        StyledText {
            text: {
                const displayName = Config.options.profile?.displayName;
                if (displayName && displayName !== "") return displayName;
                return SystemInfo.realName || SystemInfo.username;
            }
            font.pixelSize: Appearance.font.pixelSize.small
            font.weight: Font.Medium
            color: Appearance.m3colors.m3onSurface
            elide: Text.ElideRight
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
        }
        StyledText {
            text: {
                const descMode = Config.options.profile?.descriptionText || "::distro::";
                if (descMode === "::uptime::") return I18nService.tr("Up ") + DateTime.uptime;
                return SystemInfo.distroName || "Linux System";
            }
            font.pixelSize: Math.round(11 * Appearance.effectiveScale)
            font.family: Appearance.font.family.numbers
            color: Appearance.colors.colSubtext
            elide: Text.ElideRight
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
        }
    }
    
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
