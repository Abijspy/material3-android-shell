import "../../core"
import "../../services"
import "../../widgets"
import "../../core/functions" as Functions
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt.labs.folderlistmodel
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Io

                StyledNavigationRail {
                    id: sidebar
                    property Item mainSelector
                    property ListModel customFoldersModel
                    
                    showMenuButton: false
                    
                    // The rail starts expanded (like 240px wide sidebar)
                    expanded: mainSelector.sidebarExpanded
                    
                    model: {
                        let m = [];
                        let liveAvailable = WallpaperEngineService.isInstalled || MpvpaperService.isInstalled;
                        let liveEnabled = GlobalStates.wallpaperSelectorTarget === "desktop" && liveAvailable && !GameMode.active;
                        let liveTooltip = I18nService.tr("Browse live walls");
                        if (GameMode.active) liveTooltip = I18nService.tr("Live wallpapers cannot be changed while Game Mode is active");
                        else if (!liveAvailable) liveTooltip = I18nService.tr("No live wallpaper backend found");
                        else if (GlobalStates.wallpaperSelectorTarget !== "desktop") liveTooltip = I18nService.tr("Live wallpapers only supported on desktop");
                        
                        m.push({ name: I18nService.tr("Live Walls"), icon: "movie", id: "live", enabled: liveEnabled, tooltip: liveTooltip });
                        m.push({ name: I18nService.tr("Online Walls"), icon: "public", id: "online", tooltip: I18nService.tr("Browse Wallhaven and NA-ive Walls") });
                        m.push({ name: I18nService.tr("Favourites"), icon: "favorite", id: "fav", tooltip: I18nService.tr("View your favorite wallpapers") });
                        
                        m.push({ name: "Home", icon: "home", id: "local", path: Directories.home, tooltip: I18nService.tr("Browse wallpapers in %1").replace("%1", "Home") });
                        m.push({ name: "Pictures", icon: "image", id: "local", path: Directories.pictures, tooltip: I18nService.tr("Browse wallpapers in %1").replace("%1", "Pictures") });
                        m.push({ name: "Wallpapers", icon: "wallpaper", id: "local", path: Directories.home + "/Pictures/Wallpapers", tooltip: I18nService.tr("Browse wallpapers in %1").replace("%1", "Wallpapers") });
                        
                        // Reference customFoldersModel.count to trigger reactivity
                        let _count = customFoldersModel.count;
                        for (let i = 0; i < _count; i++) {
                            let folder = customFoldersModel.get(i);
                            m.push({ name: folder.name, icon: "folder", id: "local", path: folder.path, isCustom: true, tooltip: Functions.FileUtils.shortenHomePath(folder.path), rightActionIcon: "delete" });
                        }
                        
                        m.push({ name: I18nService.tr("Add Folder"), icon: "add", id: "add", tooltip: I18nService.tr("Add custom folder") });
                        return m;
                    }
                    
                    currentIndex: {
                        if (mainSelector.liveMode) return 0;
                        if (mainSelector.onlineMode) return 1;
                        if (mainSelector.favMode) return 2;
                        
                        let targetPath = mainSelector.normalizePath(Wallpapers.directory);
                        for (let i = 3; i < model.length - 1; i++) {
                            if (model[i].id === "local" && mainSelector.normalizePath(model[i].path) === targetPath) {
                                return i;
                            }
                        }
                        return -1;
                    }
                    
                    function selectTab(index) {
                        let item = model[index];
                        if (item.id === "live") mainSelector.switchMode("live");
                        else if (item.id === "online") mainSelector.switchMode("online");
                        else if (item.id === "fav") mainSelector.switchMode("fav");
                        else if (item.id === "local") {
                            mainSelector.switchMode("local");
                            Wallpapers.directory = "file://" + mainSelector.normalizePath(item.path);
                        }
                        else if (item.id === "add") Wallpapers.browseFolder();
                    }
                    
                    onItemClicked: (index) => {
                        selectTab(index);
                    }
                    
                    function cycleTab(forward) {
                        let total = model.length;
                        if (total <= 1) return;
                        
                        let nextIdx = currentIndex;
                        do {
                            if (forward) {
                                nextIdx = (nextIdx + 1) % total;
                            } else {
                                nextIdx = (nextIdx - 1 + total) % total;
                            }
                        } while (model[nextIdx].id === "add" && nextIdx !== currentIndex);
                        
                        if (nextIdx !== currentIndex && model[nextIdx].id !== "add") {
                            selectTab(nextIdx);
                        }
                    }
                    
                    onRightActionClicked: (index) => {
                        let item = model[index];
                        if (item.isCustom) {
                            let current = (Config.options.appearance.background.customFolders || []).slice();
                            const idx = current.indexOf(item.path);
                            if (idx !== -1) {
                                current.splice(idx, 1);
                                Config.options.appearance.background.customFolders = current;
                                mainSelector.refreshCustomFolders();
                            }
                        }
                    }
                }
