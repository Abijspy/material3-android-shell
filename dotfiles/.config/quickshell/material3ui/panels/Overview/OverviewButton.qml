import QtQuick
import "../../core"
import "../../services"
import "../../core"
import "../../widgets"
import "../../core"

ToggleButton {
    buttonIcon: "grid_view"
    tooltipText: I18nService.tr("Open Window Overview")

    onToggle: function () {
        if (GlobalStates.overviewOpen) {
            GlobalStates.closeAllPanels();
        } else {
            Visibilities.setActiveModule("overview");
        }
    }
}
