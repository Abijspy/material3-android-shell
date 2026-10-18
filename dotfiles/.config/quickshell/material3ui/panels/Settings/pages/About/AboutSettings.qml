import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import "../../../../core/functions" as Functions
import "."
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Widgets

Flickable {
    id: root
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    contentHeight: mainCol.implicitHeight + (48 * Appearance.effectiveScale)
    clip: true

    ScrollBar.vertical: StyledScrollBar {}

    SequentialAnimation {
        id: highlightAnim
        property var target: null
        NumberAnimation { target: highlightAnim.target; property: "opacity"; from: 1; to: 0.3; duration: 200 }
        NumberAnimation { target: highlightAnim.target; property: "opacity"; from: 0.3; to: 1; duration: 400 }
    }

    readonly property string currentView: GlobalStates.settingsAboutView

    onCurrentViewChanged: {
        root.contentY = 0
    }

    onVisibleChanged: {
        if (!visible) GlobalStates.settingsAboutView = "main"
        if (visible && GlobalStates.settingsAboutView === "main" && !dependencyPage.isScanning) {
             dependencyPage.scanDependencies();
        }
    }

    property string _version: VersionService.version

    Component.onCompleted: {
        dependencyPage.scanDependencies();
    }

    ColumnLayout {
        id: mainCol
        width: parent.width - (24 * Appearance.effectiveScale)
        spacing: 24 * Appearance.effectiveScale

        // ── Header ──
        ColumnLayout {
            spacing: 4 * Appearance.effectiveScale
            
            RowLayout {
                Layout.fillWidth: true
                spacing: 12 * Appearance.effectiveScale

                // Back Button (only in sub-pages)
                RippleButton {
                    visible: root.currentView !== "main"
                    implicitWidth: 40 * Appearance.effectiveScale
                    implicitHeight: 40 * Appearance.effectiveScale
                    buttonRadius: 20 * Appearance.effectiveScale
                    colBackground: Appearance.colors.colLayer1
                    onClicked: GlobalStates.settingsAboutView = "main"
                    contentItem: MaterialSymbol {
                        anchors.centerIn: parent
                        text: "arrow_back"
                        iconSize: 24 * Appearance.effectiveScale
                        color: Appearance.colors.colOnLayer1
                    }
                }

                StyledText {
                    text: {
                        if (GlobalStates.settingsAboutView === "main") return I18nService.tr("About")
                        if (GlobalStates.settingsAboutView === "update") return I18nService.tr("Shell Update")
                        if (GlobalStates.settingsAboutView === "archupdates") return I18nService.tr("Packages & Updates")
                        if (GlobalStates.settingsAboutView === "dependency") return I18nService.tr("Dependency Check")
                        if (GlobalStates.settingsAboutView === "credits") return I18nService.tr("Special Thanks")
                        return I18nService.tr("About")
                    }
                    font.pixelSize: Appearance.font.pixelSize.huge
                    font.family: Appearance.font.family.title
                    font.weight: Font.DemiBold
                    color: Appearance.colors.colOnLayer1
                    Layout.fillWidth: true
                }
            }

            StyledText {
                text: {
                    if (GlobalStates.settingsAboutView === "main") return I18nService.tr("System information and shell details.")
                    if (GlobalStates.settingsAboutView === "update") return I18nService.tr("Manage shell update channels and fetch new versions.")
                    if (GlobalStates.settingsAboutView === "archupdates") return I18nService.tr("Update Arch, AUR and Flatpak packages, or find and install new ones.")
                    if (GlobalStates.settingsAboutView === "dependency") return I18nService.tr("Check and install missing system dependencies.")
                    if (GlobalStates.settingsAboutView === "credits") return I18nService.tr("Contributors and projects that made Material3UI possible.")
                    return ""
                }
                font.pixelSize: Appearance.font.pixelSize.normal
                color: Appearance.colors.colSubtext
            }
        }

        // ── Main View ──
        AboutMainView {
            visible: GlobalStates.settingsAboutView === "main"
            Layout.fillWidth: true
            version: root._version
            onPushView: (view) => GlobalStates.settingsAboutView = view
        }

        // ── Update Sub-page ──
        AboutUpdate {
            visible: GlobalStates.settingsAboutView === "update"
            Layout.fillWidth: true
        }

        // ── Packages & updates Sub-page ──
        AboutArchUpdates {
            visible: GlobalStates.settingsAboutView === "archupdates"
            Layout.fillWidth: true
        }

        // ── Dependency Sub-page ──
        AboutDependency {
            id: dependencyPage
            visible: GlobalStates.settingsAboutView === "dependency"
            Layout.fillWidth: true
        }

        // ── Credits Sub-page ──
        AboutCredits {
            visible: GlobalStates.settingsAboutView === "credits"
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }
    }
}
