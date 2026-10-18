pragma Singleton
import QtQuick
import Quickshell

Singleton {
    id: root
    
    function screenshot() { Quickshell.execDetached(["qs", "-c", "material3ui", "ipc", "call", "region", "screenshot"]) }
    function search() { Quickshell.execDetached(["qs", "-c", "material3ui", "ipc", "call", "region", "search"]) }
    function ocr() { Quickshell.execDetached(["qs", "-c", "material3ui", "ipc", "call", "region", "ocr"]) }
    function record() { Quickshell.execDetached(["qs", "-c", "material3ui", "ipc", "call", "region", "record"]) }
    function recordWithSound() { Quickshell.execDetached(["qs", "-c", "material3ui", "ipc", "call", "region", "recordWithSound"]) }
    function recordFullscreenWithSound() { Quickshell.execDetached(["qs", "-c", "material3ui", "ipc", "call", "region", "recordFullscreenWithSound"]) }
    function qrcode() { Quickshell.execDetached(["qs", "-c", "material3ui", "ipc", "call", "region", "qrcode"]) }
}
