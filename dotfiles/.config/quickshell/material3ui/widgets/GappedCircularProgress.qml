import QtQuick
import "../core"

Canvas {
    id: root

    property real progress: 0
    property color colPrimary: Appearance.m3colors.m3primary
    property color colSecondary: Appearance.m3colors.m3surfaceVariant
    property real strokeWidth: 10 * Appearance.effectiveScale

    onProgressChanged: requestPaint()
    onColPrimaryChanged: requestPaint()
    onColSecondaryChanged: requestPaint()
    
    Connections {
        target: Appearance
        function onM3colorsChanged() { root.requestPaint(); }
        function onEffectiveScaleChanged() { root.requestPaint(); }
    }

    onPaint: {
        const ctx = getContext("2d");
        ctx.clearRect(0, 0, width, height);

        const cx = width / 2, cy = height / 2;
        const lw = strokeWidth;
        const r = Math.min(cx, cy) - lw;
        
        // Gap must be slightly larger than lineWidth to prevent round caps from overlapping
        const gapLinear = lw + 4 * Appearance.effectiveScale;
        const gapAngle = gapLinear / r;
        const startAngle = -Math.PI / 2;
        
        const totalSweep = Math.PI * 2;
        let activeSweep = progress * totalSweep;
        
        ctx.lineWidth = lw;
        ctx.lineCap = "round";

        if (progress <= 0.00001) {
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.strokeStyle = colSecondary;
            ctx.stroke();
            return;
        }

        if (progress >= 0.99999) {
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.strokeStyle = colPrimary;
            ctx.stroke();
            return;
        }

        // We need a minimum sweep for both tracks so they don't draw backwards.
        // The minimum sweep is `gapAngle` (so that sweep - gapAngle >= 0),
        // which renders a perfect circular dot when actualSweep is 0.
        let minSweep = gapAngle;
        
        let drawActive = activeSweep;
        let drawInactive = totalSweep - activeSweep;
        
        if (drawActive < minSweep) {
            drawActive = minSweep;
            drawInactive = totalSweep - drawActive;
        } else if (drawInactive < minSweep) {
            drawInactive = minSweep;
            drawActive = totalSweep - drawInactive;
        }

        // Active track
        const actualActiveSweep = Math.max(0.001, drawActive - gapAngle);
        ctx.beginPath();
        ctx.arc(cx, cy, r, startAngle + gapAngle/2, startAngle + gapAngle/2 + actualActiveSweep);
        ctx.strokeStyle = colPrimary;
        ctx.stroke();

        // Inactive track
        const actualInactiveSweep = Math.max(0.001, drawInactive - gapAngle);
        ctx.beginPath();
        ctx.arc(cx, cy, r, startAngle + drawActive + gapAngle/2, startAngle + drawActive + gapAngle/2 + actualInactiveSweep);
        ctx.strokeStyle = colSecondary;
        ctx.stroke();
    }
}
