import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import "../../core"
import "../../services"

/**
 * Animated Weather Background - Overlay version for Android 16.
 * Improved logic ported from Ambxst for stability and aesthetics.
 */
Item {
    id: root

    property bool animationsEnabled: visible
    property bool backgroundEnabled: false

    // Internal Time Calculation (Synced with system)
    property real currentHour: DateTime.hours + DateTime.minutes / 60

    // Internal Time Blending Logic (Ambxst style)
    function calculateTimeBlend(hour) {
        var d = 0, e = 0, n = 0;
        if (hour >= 9 && hour <= 17) { d = 1.0; }
        else if (hour > 8 && hour < 9) { var t = hour - 8; e = 1.0 - t; d = t; }
        else if (hour > 17 && hour < 18) { var t = hour - 17; d = 1.0 - t; e = t; }
        else if (hour >= 6 && hour <= 8) { e = 1.0; }
        else if (hour >= 18 && hour <= 20) { e = 1.0; }
        else if (hour > 5 && hour < 6) { var t2 = hour - 5; n = 1.0 - t2; e = t2; }
        else if (hour > 20 && hour < 21) { var t3 = hour - 20; e = 1.0 - t3; n = t3; }
        else { n = 1.0; }
        return { d: d, e: e, n: n };
    }

    readonly property var blend: calculateTimeBlend(currentHour)

    function blendColors(c1, c2, c3, b) {
        var r = c1.r * b.d + c2.r * b.e + c3.r * b.n;
        var g = c1.g * b.d + c2.g * b.e + c3.g * b.n;
        var bv = c1.b * b.d + c2.b * b.e + c3.b * b.n;
        return Qt.rgba(r, g, bv, 1);
    }

    // Sky Colors (Only used if backgroundEnabled is true)
    readonly property color dayTop: "#87CEEB"; readonly property color dayBot: "#E0F6FF"
    readonly property color eveTop: "#1a1a2e"; readonly property color eveBot: "#ffeaa7"
    readonly property color nigTop: "#0f0f23"; readonly property color nigBot: "#2d2d5a"
    readonly property color topC: blendColors(dayTop, eveTop, nigTop, blend)
    readonly property color botC: blendColors(dayBot, eveBot, nigBot, blend)

    readonly property string weatherEffect: {
        let icon = (Weather.current.icon || "").toLowerCase();
        if (icon.indexOf("clear") !== -1) return "clear";
        if (icon.indexOf("cloudy") !== -1 || icon.indexOf("clouds") !== -1) return "clouds";
        if (icon.indexOf("rain") !== -1 || icon.indexOf("drizzle") !== -1 || icon.indexOf("showers") !== -1) return "rain";
        if (icon.indexOf("snow") !== -1 || icon.indexOf("flurries") !== -1) return "snow";
        if (icon.indexOf("thunder") !== -1) return "thunderstorm";
        if (icon.indexOf("haze") !== -1 || icon.indexOf("fog") !== -1) return "fog";
        return "clouds";
    }

    readonly property real weatherIntensity: {
        let cond = (Weather.current.condition || "").toLowerCase();
        if (cond.indexOf("heavy") !== -1) return 1.0;
        if (cond.indexOf("moderate") !== -1) return 0.6;
        return 0.4;
    }

    // Flat-bottomed cloud silhouette with three distinct lobes (aspect 2:1).
    // Fill shapes stay fully opaque; transparency is applied once on this
    // container so overlapping parts never double-darken.
    component CloudShape: Item {
        id: cloudShapeRoot
        property color fillColor

        Rectangle {
            x: parent.width * 0.08; y: parent.height * 0.55
            width: parent.width * 0.84; height: parent.height * 0.45
            radius: height / 2
            color: cloudShapeRoot.fillColor
        }
        Rectangle {
            x: parent.width * 0.10; y: parent.height * 0.48
            width: parent.width * 0.26; height: width
            radius: width / 2
            color: cloudShapeRoot.fillColor
        }
        Rectangle {
            x: parent.width * 0.30; y: parent.height * 0.28
            width: parent.width * 0.36; height: width
            radius: width / 2
            color: cloudShapeRoot.fillColor
        }
        Rectangle {
            x: parent.width * 0.56; y: parent.height * 0.40
            width: parent.width * 0.30; height: width
            radius: width / 2
            color: cloudShapeRoot.fillColor
        }
    }

    Item {
        id: rootContents
        anchors.fill: parent

        // --- Optional Sky Background ---
        Rectangle {
            anchors.fill: parent
            visible: root.backgroundEnabled
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.topC }
                GradientStop { position: 1.0; color: root.botC }
            }
        }

        // --- Weather Elements Layer ---
        Item {
            anchors.fill: parent

            // 1. Stars (Visible at night when weather is clear)
            Item {
                id: starsEffect; anchors.fill: parent
                opacity: (root.blend.n > 0.3 && root.weatherEffect === "clear") ? Math.min(1, (root.blend.n - 0.3) / 0.4) : 0
                visible: opacity > 0

                Repeater {
                    model: 30
                    Rectangle {
                        property real baseX: Math.random() * starsEffect.width
                        property real baseY: Math.random() * (starsEffect.height * 0.7)
                        x: baseX; y: baseY; width: (1.5 + Math.random()) * Appearance.effectiveScale; height: width; radius: width/2; color: "white"
                        opacity: 0.5 + Math.random() * 0.5

                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            running: starsEffect.visible && root.animationsEnabled
                            NumberAnimation { to: 0.2; duration: 1000 + Math.random() * 2000; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 1000 + Math.random() * 2000; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }

            // 2. Clouds (Layered blobs drifting in one wind direction)
            Item {
                id: cloudEffect; anchors.fill: parent
                visible: ["clouds", "rain", "snow", "thunderstorm", "fog"].includes(root.weatherEffect)

                property color cloudColor: Appearance.m3colors.m3onSurface

                // Background layer: large, slow, faint.
                // Spawn positions are staggered across the travel path so
                // clouds never bunch up on the same spot.
                Repeater {
                    model: 3
                    CloudShape {
                        id: bgCloud
                        required property int index
                        readonly property real baseY: (10 + index * 30) * Appearance.effectiveScale
                        readonly property real travelDist: cloudEffect.width + width + 160 * Appearance.effectiveScale
                        readonly property real targetOpacity: 0.07 + index * 0.012
                        // Phase 0..1 along the travel path; wraps always happen
                        // offscreen, stagger only offsets the starting position
                        property real phase: index / 3
                        property real fade: 0
                        width: (230 - index * 30) * Appearance.effectiveScale
                        height: width * 0.5
                        fillColor: cloudEffect.cloudColor
                        opacity: targetOpacity * fade
                        y: baseY
                        x: cloudEffect.width + 80 * Appearance.effectiveScale - phase * travelDist

                        // Fade in on creation/reveal so staggered mid-screen
                        // starts never blink into existence
                        NumberAnimation on fade {
                            running: cloudEffect.visible && root.animationsEnabled
                            from: 0
                            to: 1
                            duration: 1500
                        }

                        FrameAnimation {
                            running: cloudEffect.visible && root.animationsEnabled
                            onTriggered: bgCloud.phase = (bgCloud.phase + frameTime * 1000 / (55000 + bgCloud.index * 12000)) % 1
                        }

                        SequentialAnimation on y {
                            loops: Animation.Infinite
                            running: cloudEffect.visible && root.animationsEnabled
                            NumberAnimation { to: bgCloud.baseY + 4 * Appearance.effectiveScale; duration: 7000 + bgCloud.index * 1100; easing.type: Easing.InOutSine }
                            NumberAnimation { to: bgCloud.baseY; duration: 7000 + bgCloud.index * 1100; easing.type: Easing.InOutSine }
                        }
                    }
                }

                // Foreground layer: smaller, faster, more present
                Repeater {
                    model: 3
                    CloudShape {
                        id: fgCloud
                        required property int index
                        readonly property real baseY: (54 + index * 24) * Appearance.effectiveScale
                        readonly property real travelDist: cloudEffect.width + width + 120 * Appearance.effectiveScale
                        readonly property real targetOpacity: 0.10 + index * 0.012
                        property real phase: index / 3
                        property real fade: 0
                        width: (160 - (index % 2) * 30) * Appearance.effectiveScale
                        height: width * 0.5
                        fillColor: cloudEffect.cloudColor
                        opacity: targetOpacity * fade
                        y: baseY
                        x: cloudEffect.width + 60 * Appearance.effectiveScale - phase * travelDist

                        NumberAnimation on fade {
                            running: cloudEffect.visible && root.animationsEnabled
                            from: 0
                            to: 1
                            duration: 1200
                        }

                        FrameAnimation {
                            running: cloudEffect.visible && root.animationsEnabled
                            onTriggered: fgCloud.phase = (fgCloud.phase + frameTime * 1000 / (34000 + fgCloud.index * 8000)) % 1
                        }

                        SequentialAnimation on y {
                            loops: Animation.Infinite
                            running: cloudEffect.visible && root.animationsEnabled
                            NumberAnimation { to: fgCloud.baseY + 3 * Appearance.effectiveScale; duration: 5600 + fgCloud.index * 900; easing.type: Easing.InOutSine }
                            NumberAnimation { to: fgCloud.baseY; duration: 5600 + fgCloud.index * 900; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }

            // 3. Rain (Continuous fall, desynced spawns, slight wind tilt)
            Item {
                id: rainEffect; anchors.fill: parent
                visible: root.weatherEffect === "rain" || root.weatherEffect === "thunderstorm"

                property color dropColor: Appearance.m3colors.m3onSurface

                Repeater {
                    model: 42
                    Rectangle {
                        id: rainDrop
                        required property int index
                        readonly property real fallDuration: 550 + Math.random() * 350
                        readonly property real spawnOffset: Math.random() * rainEffect.height
                        readonly property real startX: Math.random() * rainEffect.width
                        // Horizontal drift matching the drop tilt (tan(8deg) ~ 0.14)
                        // so drops travel along their own axis, like wind-blown rain
                        readonly property real drift: (rainEffect.height + height * 2) * 0.14

                        x: startX + drift
                        y: -height - spawnOffset
                        width: 1.2 * Appearance.effectiveScale
                        height: (14 + Math.random() * 10) * Appearance.effectiveScale
                        radius: width / 2
                        color: rainEffect.dropColor
                        opacity: 0.16 + Math.random() * 0.16
                        rotation: 8

                        NumberAnimation on y {
                            loops: Animation.Infinite
                            running: rainEffect.visible && root.animationsEnabled
                            from: -rainDrop.height - rainDrop.spawnOffset
                            to: rainEffect.height + rainDrop.height
                            duration: rainDrop.fallDuration
                            easing.type: Easing.Linear
                        }

                        NumberAnimation on x {
                            loops: Animation.Infinite
                            running: rainEffect.visible && root.animationsEnabled
                            from: rainDrop.startX + rainDrop.drift
                            to: rainDrop.startX
                            duration: rainDrop.fallDuration
                            easing.type: Easing.Linear
                        }
                    }
                }
            }

            // 4. Snow (Adaptive color, varied size, organic sway)
            Item {
                id: snowEffect; anchors.fill: parent
                visible: root.weatherEffect === "snow"

                Repeater {
                    model: 26
                    Rectangle {
                        id: snowFlake
                        required property int index
                        readonly property real fallDuration: 8000 + Math.random() * 7000
                        readonly property real baseX: Math.random() * snowEffect.width
                        readonly property real spawnOffset: Math.random() * snowEffect.height
                        readonly property real swayAmp: (6 + Math.random() * 12) * Appearance.effectiveScale

                        x: baseX
                        y: -width - spawnOffset
                        width: (3.5 + Math.random() * 3) * Appearance.effectiveScale
                        height: width
                        radius: width / 2
                        color: Appearance.m3colors.m3onSurface
                        opacity: 0.35 + Math.random() * 0.3

                        NumberAnimation on y {
                            loops: Animation.Infinite
                            running: snowEffect.visible && root.animationsEnabled
                            from: -snowFlake.width - snowFlake.spawnOffset
                            to: snowEffect.height + snowFlake.width
                            duration: snowFlake.fallDuration
                            easing.type: Easing.Linear
                        }

                        SequentialAnimation on x {
                            loops: Animation.Infinite
                            running: snowEffect.visible && root.animationsEnabled
                            NumberAnimation { to: snowFlake.baseX + snowFlake.swayAmp; duration: snowFlake.fallDuration / 2; easing.type: Easing.InOutSine }
                            NumberAnimation { to: snowFlake.baseX - snowFlake.swayAmp; duration: snowFlake.fallDuration; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }

            // 5. Thunderstorm Lightning
            Rectangle {
                id: lightningFlash
                anchors.fill: parent
                color: "white"
                opacity: 0
                visible: root.weatherEffect === "thunderstorm"

                SequentialAnimation {
                    loops: Animation.Infinite
                    running: lightningFlash.visible && root.animationsEnabled
                    PauseAnimation { duration: 5000 + Math.random() * 7000 }
                    NumberAnimation { target: lightningFlash; property: "opacity"; to: 0.45; duration: 60 }
                    NumberAnimation { target: lightningFlash; property: "opacity"; to: 0; duration: 140 }
                    NumberAnimation { target: lightningFlash; property: "opacity"; to: 0.25; duration: 60 }
                    NumberAnimation { target: lightningFlash; property: "opacity"; to: 0; duration: 320 }
                    PauseAnimation { duration: 2800 }
                }
            }
        }
    }
}
