pragma Singleton
import Quickshell

Singleton {
    id: root

    function trimFileProtocol(str) {
        let s = str;
        if (typeof s !== "string") s = str.toString();
        return s.startsWith("file://") ? s.slice(7) : s;
    }

    function fileNameForPath(str) {
        if (typeof str !== "string") return "";
        const trimmed = trimFileProtocol(str);
        return trimmed.split(/[/\\]/).pop();
    }

    function parentDirectory(str) {
        if (typeof str !== "string") return "";
        const trimmed = trimFileProtocol(str);
        const parts = trimmed.split(/[/\\]/);
        if (parts.length <= 1) return "";
        parts.pop();
        return parts.join("/");
    }

    function shortenHomePath(str) {
        if (typeof str !== "string" || str === "") return str;
        let s = str.startsWith("file://") ? str.slice(7) : str;
        if (s.endsWith("/")) s = s.slice(0, -1);
        const home = Quickshell.env("HOME");
        if (home === "" || !home) return s;
        if (s === home) return "~";
        if (s.startsWith(home + "/")) return "~" + s.slice(home.length);
        return s;
    }

    function expandHomePath(str) {
        if (typeof str !== "string" || str === "") return str;
        const home = Quickshell.env("HOME");
        if (home === "" || !home) return str;
        if (str === "~") return home;
        if (str.startsWith("~/")) return home + str.slice(1);
        return str;
    }
}
