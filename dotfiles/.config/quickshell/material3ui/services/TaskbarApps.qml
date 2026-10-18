pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Wayland
import "../core"
import "../services"

/**
 * TaskbarApps Service
 * Fixed: Ensured new apps are immediately detected and added to the model.
 */
Singleton {
    id: root

    property var _entryCache: ({})
    property list<string> unpinnedOrder: []
    // Bumped on every pin change so UI bindings (e.g. launcher favorites) can re-evaluate.
    property int pinVersion: 0

    // Resolve any app id (desktop entry id, window class/appId, or display name)
    // into the matching DesktopEntry. Falls back to a robust multi-step lookup
    // instead of the weak built-in heuristicLookup().
    function resolveEntry(appId) {
        if (!appId) return null;
        if (_entryCache[appId]) return _entryCache[appId];
        const entry = root.lookupEntry(appId);
        if (entry) _entryCache[appId] = entry;
        return entry;
    }

    function lookupEntry(appId) {
        const lowId = appId.toLowerCase();

        const exact = DesktopEntries.byId(appId);
        if (exact && !exact.noDisplay) return exact;
        // Remember a NoDisplay exact match as fallback (e.g. hidden spotify.desktop
        // shadowing the visible spotify-adblock.desktop that shares StartupWMClass).

        // Window classes that differ from desktop entry ids (e.g. brave-browser -> brave-desktop)
        if (AppSearch.substitutions && AppSearch.substitutions[lowId]) {
            const sub = DesktopEntries.byId(AppSearch.substitutions[lowId]);
            if (sub && !sub.noDisplay) return sub;
        }

        // DesktopEntries.applications already excludes NoDisplay entries, so the
        // scans below always prefer visible entries over hidden ones.
        const apps = Array.from(DesktopEntries.applications.values);

        // StartupWMClass / id match
        for (const app of apps) {
            if (app.startupClass && app.startupClass.toLowerCase() === lowId) return app;
            if (app.id && app.id.toLowerCase() === lowId) return app;
        }

        // Exact display-name match (e.g. window class "Brave Browser" -> entry named "Brave Browser")
        for (const app of apps) {
            if (app.name && app.name.toLowerCase() === lowId) return app;
            if (app.genericName && app.genericName.toLowerCase() === lowId) return app;
        }

        // Executable basename match (e.g. window class "code" -> entry executing "code")
        for (const app of apps) {
            if (app.command && app.command.length > 0) {
                const execBase = String(app.command[0]).split("/").pop().toLowerCase();
                if (execBase === lowId) return app;
            }
        }

        return exact || null;
    }

    function getDesktopEntry(appId) {
        return root.resolveEntry(appId);
    }

    // Canonical identity used for matching pins and running apps.
    function resolveId(appId) {
        if (!appId) return appId;
        const entry = root.resolveEntry(appId);
        return entry ? entry.id.toLowerCase() : appId.toLowerCase();
    }

    // Normalize a running window's appId to its desktop entry id so pinned
    // favorites and running windows of the same app always merge.
    function normalizeAppId(appId) {
        return root.resolveId(appId);
    }

    function isPinned(appId) {
        if (!Config.ready) return false;
        const id = root.resolveId(appId);
        // Resolve both sides so legacy pins (e.g. "spotify") match their canonical id (e.g. "spotify-adblock").
        return Config.options.dock.pinnedApps.map(p => root.resolveId(p).toLowerCase()).indexOf(id) !== -1;
    }

    function togglePin(appId) {
        if (!Config.ready || !appId) return;
        const entry = root.resolveEntry(appId);
        const storeId = entry ? entry.id : appId;
        const storeLow = storeId.toLowerCase();

        // Normalize existing pins to their canonical ids first (migrates legacy entries).
        let pinned = Array.from(Config.options.dock.pinnedApps).map(p => root.resolveId(p));

        const idx = pinned.map(p => p.toLowerCase()).indexOf(storeLow);
        if (idx !== -1) pinned.splice(idx, 1);
        else pinned.push(storeId);
        Config.options.dock.pinnedApps = pinned;
        root.pinVersion++;
    }

    function moveApp(appId, direction) {
        if (!appId || !Config.ready) return;
        const pinnedApps = Array.from(Config.options.dock.pinnedApps);
        const lowId = appId.toLowerCase();
        const resolvedPinned = pinnedApps.map(p => root.resolveId(p).toLowerCase());
        const isPinned = resolvedPinned.includes(lowId);
        
        if (isPinned) {
            const idx = resolvedPinned.indexOf(lowId);
            const target = idx + direction;
            if (target >= 0 && target < pinnedApps.length) {
                pinnedApps.splice(idx, 1);
                pinnedApps.splice(target, 0, appId);
                Config.options.dock.pinnedApps = pinnedApps;
            }
        } else {
            const unpinned = Array.from(root.unpinnedOrder);
            const idx = unpinned.indexOf(lowId);
            if (idx === -1) return;
            const target = idx + direction;
            if (target >= 0 && target < unpinned.length) {
                unpinned.splice(idx, 1);
                unpinned.splice(target, 0, lowId);
                root.unpinnedOrder = unpinned;
            }
        }
    }

    // Main Model Binding
    property list<var> apps: {
        if (!Config.ready) return [];
        
        // FORCED TRIGGERS: Ensure any change in toplevels triggers a rebuild
        const _count = ToplevelManager.toplevels.values.length; 
        const _toplevels = ToplevelManager.toplevels.values;
        const _entryCount = DesktopEntries.applications.values.length;
        const pinnedApps = Config.options.dock.pinnedApps ?? [];
        const ignoredRegexStrings = Config.options.dock.ignoredAppRegexes ?? [];
        const ignoredRegexes = ignoredRegexStrings.map(pattern => new RegExp(pattern, "i"));

        // Normalize pinned ids to their canonical desktop entry ids so they merge
        // with running windows and launch the right (visible) entry.
        // E.g. pinned "spotify" becomes "spotify-adblock" because spotify.desktop is NoDisplay.
        const normalizedPinned = pinnedApps.map(p => root.resolveId(p));
        const normalizedPinnedSet = normalizedPinned.map(p => p.toLowerCase());
        if (pinnedApps.length !== normalizedPinned.length ||
            pinnedApps.some((p, i) => p.toLowerCase() !== normalizedPinned[i])) {
            Qt.callLater(() => { Config.options.dock.pinnedApps = normalizedPinned; });
        }

        const map = new Map();
        let currentRunningIds = [];

        // 1. Process Pinned Apps
        for (const appId of normalizedPinned) {
            const id = appId.toLowerCase();
            if (!map.has(id)) {
                map.set(id, { appId: id, pinned: true, toplevels: [] });
            }
        }

        // 2. Process Running Windows (Wayland Toplevels)
        for (const toplevel of _toplevels) {
            if (!toplevel || !toplevel.appId) continue;
            if (ignoredRegexes.some(re => re.test(toplevel.appId))) continue;
            
            const id = root.normalizeAppId(toplevel.appId);
            if (!currentRunningIds.includes(id)) currentRunningIds.push(id);

            if (!map.has(id)) {
                map.set(id, { appId: id, pinned: false, toplevels: [] });
            }
            
            // Add toplevel if not already present in the list for this appId
            const existingToplevels = map.get(id).toplevels;
            if (!existingToplevels.includes(toplevel)) {
                existingToplevels.push(toplevel);
            }
        }

        // 3. Sync unpinnedOrder
        let updatedUnpinnedOrder = root.unpinnedOrder.filter(id => {
            // Keep if still running and not pinned
            return currentRunningIds.includes(id) && !normalizedPinnedSet.includes(id);
        });

        // Add any NEWLY opened apps to the end of the unpinned order
        for (const id of currentRunningIds) {
            if (!normalizedPinnedSet.includes(id) && !updatedUnpinnedOrder.includes(id)) {
                updatedUnpinnedOrder.push(id);
            }
        }

        // Apply unpinned order update if it changed
        if (JSON.stringify(updatedUnpinnedOrder) !== JSON.stringify(root.unpinnedOrder)) {
            Qt.callLater(() => { root.unpinnedOrder = updatedUnpinnedOrder; });
        }

        // 4. Final Ordered List of IDs
        let orderedIds = [];
        for (const id of normalizedPinned) orderedIds.push(id.toLowerCase());
        for (const id of updatedUnpinnedOrder) {
            if (!orderedIds.includes(id)) orderedIds.push(id);
        }

        // 5. Map to persistent Pool Objects
        let finalResult = [];
        for (const id of orderedIds) {
            const data = map.get(id);
            if (!data) continue;

            let wrapper = null;
            for (let i = 0; i < pool.length; i++) {
                if (pool[i] && pool[i].appId === id) {
                    wrapper = pool[i];
                    break;
                }
            }

            if (!wrapper) {
                wrapper = appEntryComp.createObject(root, { appId: id });
                pool.push(wrapper);
            }

            wrapper.toplevels = data.toplevels;
            wrapper.pinned = data.pinned;
            finalResult.push(wrapper);
        }

        // 6. Cleanup Pool (Deferred)
        Qt.callLater(() => {
            for (let i = pool.length - 1; i >= 0; i--) {
                if (pool[i] && !finalResult.includes(pool[i])) {
                    const old = pool.splice(i, 1)[0];
                    if (old) old.destroy();
                }
            }
        });

        return finalResult;
    }

    property var pool: []

    Connections {
        target: DesktopEntries.applications
        function onValuesChanged() {
            root._entryCache = {};
        }
    }

    Component {
        id: appEntryComp
        QtObject {
            property string appId: ""
            property list<var> toplevels: []
            property bool pinned: false
        }
    }
}
