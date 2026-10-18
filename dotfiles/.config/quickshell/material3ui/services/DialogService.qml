pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell

Singleton {
    id: root

    property bool active: false
    property string titleText: ""
    property string messageText: ""
    property string confirmText: I18nService.tr("OK")
    property string cancelText: I18nService.tr("Cancel")
    property string iconText: "warning"
    property bool isDestructive: false

    // Custom content mode: when set, DialogPanel renders this component instead
    // of the standard icon/title/message/buttons layout. The component's own
    // buttons call DialogService.submit()/cancel() to close.
    property var contentComponent: null
    property real contentWidth: 0 // dialog width override for custom content (pre-scale)
    // Optional ESC interceptor for custom content (e.g. close a nested picker
    // before closing the dialog itself). Called instead of cancel() when set.
    property var escapeHandler: null

    property var _confirmCallback: null
    property var _cancelCallback: null

    function requestConfirmation(options, onConfirm, onCancel) {
        root.titleText = options.titleText || I18nService.tr("Are you sure?");
        root.messageText = options.messageText || I18nService.tr("This action cannot be undone.");
        root.confirmText = options.confirmText || I18nService.tr("OK");
        root.cancelText = options.cancelText || I18nService.tr("Cancel");
        root.iconText = options.iconText !== undefined ? options.iconText : "warning";
        root.isDestructive = options.isDestructive !== undefined ? options.isDestructive : false;

        root.contentComponent = null;
        root.contentWidth = 0;
        root.escapeHandler = null;
        root._confirmCallback = onConfirm;
        root._cancelCallback = onCancel;

        root.active = true;
    }

    function requestCustom(component, width) {
        root.contentComponent = component;
        root.contentWidth = width || 0;
        root.escapeHandler = null; // content may re-set it on completion
        root._confirmCallback = null;
        root._cancelCallback = null;
        root.active = true;
    }

    function submit() {
        if (!root.active) return;
        root.active = false;
        root.contentComponent = null;
        root.contentWidth = 0;
        if (root._confirmCallback) {
            root._confirmCallback();
        }
    }

    function cancel() {
        if (!root.active) return;
        root.active = false;
        root.contentComponent = null;
        root.contentWidth = 0;
        if (root._cancelCallback) {
            root._cancelCallback();
        }
    }
}
