import "../../../services"
import QtQuick
import "../../widgets"
import ".."

OsdValueIndicator {
    id: osdValues
    value: Audio.microphoneVolume
    icon: Audio.microphoneMuted ? "mic_off" : "mic"
    name: "Microphone"
}
