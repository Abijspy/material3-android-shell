import QtQuick
import Qt5Compat.GraphicalEffects
import "../core"
import "../services"

/**
 * Thumbnail widget for video wallpapers.
 * Shows a film-icon placeholder while the ffmpeg-generated thumbnail is being created.
 */
Item {
    id: root

    required property string videoPath
    property real radius: 10 * Appearance.effectiveScale

    readonly property string cleanPath: {
        if (!videoPath) return "";
        let p = videoPath.toString();
        if (p.startsWith("file://")) p = p.substring(7);
        return p;
    }

    readonly property string thumbUrl: {
        if (cleanPath === "") return "";
        return MpvpaperService.previewFor(cleanPath);
    }

    property bool _requested: false
    property int _reloadToken: 0

    // Placeholder background + film icon
    Rectangle {
        anchors.fill: parent
        color: Appearance.colors.colLayer2
        radius: root.radius
        visible: !img.visible

        MaterialSymbol {
            anchors.centerIn: parent
            text: "movie"
            iconSize: 32 * Appearance.effectiveScale
            color: Appearance.colors.colSubtext
        }
    }

    Image {
        id: img
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        smooth: true
        source: root.thumbUrl !== "" ? (root.thumbUrl + (root._reloadToken > 0 ? "?v=" + root._reloadToken : "")) : ""
        visible: status === Image.Ready
        opacity: visible ? 1 : 0

        onStatusChanged: {
            if (status === Image.Error && root.thumbUrl !== "") {
                if (!root._requested && root.cleanPath !== "") {
                    root._requested = true;
                    MpvpaperService.requestThumbnail(root.cleanPath);
                }
            }
        }
    }

    // Play badge
    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8 * Appearance.effectiveScale
        width: 24 * Appearance.effectiveScale
        height: 24 * Appearance.effectiveScale
        radius: width / 2
        color: Qt.rgba(0, 0, 0, 0.5)
        visible: img.visible || !root._requested

        MaterialSymbol {
            anchors.centerIn: parent
            text: "movie" // Menggunakan icon movie ala badge
            iconSize: 14 * Appearance.effectiveScale
            color: "white"
        }
    }

    Connections {
        target: MpvpaperService
        function onThumbnailGenerated(p) {
            if (p === root.cleanPath) {
                root._requested = true;
                root._reloadToken = root._reloadToken + 1;
            }
        }
    }
}