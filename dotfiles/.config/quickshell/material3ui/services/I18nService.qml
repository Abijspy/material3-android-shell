pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import "../core"

Singleton {
    id: root

    property var translations: ({})
    property var generatedTranslations: ({})
    property var availableLanguages: ["en_US"]
    property var availableGeneratedLanguages: []
    property var availableLanguageNames: ({})
    property var allAvailableLanguages: {
        const combined = new Set([...root.availableLanguages, ...root.availableGeneratedLanguages]);
        return Array.from(combined).sort();
    }
    property bool isScanning: scanLanguagesProcess.running
    property bool isLoading: false
    property string translationKeepSuffix: "/*keep*/"
    
    // Hardcoded for safety to ensure it points to the material3ui translations dir
    property string translationsDir: Quickshell.shellPath("translations")
    property string generatedTranslationsDir: Directories.shellConfig + "/translations"

    property string languageCode: {
        var configLang = Config?.options?.language?.ui ?? "auto";

        if (configLang !== "auto")
            return configLang;

        return Qt.locale().name;
    }

    TranslationScanner {
        id: scanLanguagesProcess
        translationsDir: root.translationsDir
        onLanguagesScanned: (languages, names) => {
            root.availableLanguages = [...languages];
            if (names) root.availableLanguageNames = Object.assign({}, root.availableLanguageNames, names);
        }
    }

    TranslationScanner {
        id: scanGeneratedLanguagesProcess
        translationsDir: root.generatedTranslationsDir
        onLanguagesScanned: (languages, names) => {
            root.availableGeneratedLanguages = [...languages];
            if (names) root.availableLanguageNames = Object.assign({}, root.availableLanguageNames, names);
        }
    }

    onLanguageCodeChanged: {
        translationFileView.languageCode = root.languageCode;
        translationFileView.reread();
        root.syncGeneratedTranslations();
    }
    onAvailableGeneratedLanguagesChanged: updateTranslations()

    function syncGeneratedTranslations() {
        const hasLang = root.availableGeneratedLanguages.indexOf(root.languageCode) !== -1;
        generatedTranslationFileView.languageCode = hasLang ? root.languageCode : "";
        generatedTranslationFileView.reread();
    }

    function updateTranslations() {
        if (!root.languageCode) return;
        translationFileView.languageCode = root.languageCode;
        translationFileView.reread();
        root.syncGeneratedTranslations();
    }

    TranslationReader {
        id: translationFileView
        translationsDir: root.translationsDir
        languageCode: root.languageCode
        onContentLoaded: (data) => {
            root.translations = data;
            root.isLoading = false;
        }
    }

    TranslationReader {
        id: generatedTranslationFileView
        translationsDir: root.generatedTranslationsDir
        languageCode: root.languageCode
        onContentLoaded: (data) => {
            root.generatedTranslations = data;
            root.isLoading = false;
        }
    }

    function tr(text) {
        // Special cases
        if (!text) return "";
        var key = text.toString();
        if (root.isLoading || (!root?.translations?.hasOwnProperty(key) && !root?.generatedTranslations?.hasOwnProperty(key)))
            return key;
        
        // Normal cases
        var translation = root.translations[key] || root.generatedTranslations[key] || key;
        
        if (typeof translation === 'string' && translation.endsWith(root.translationKeepSuffix)) {
            translation = translation.substring(0, translation.length - root.translationKeepSuffix.length).trim();
        }
        return translation;
    }

    function languageName(code) {
        if (!code || code === "auto") return "";
        // Optional per-file override (Language Name key); otherwise derive
        // the native name automatically from Qt's locale database so that
        // adding a new language = just dropping a translation JSON.
        const name = root.availableLanguageNames[code];
        if (name && name !== code) return name;
        try {
            const loc = Qt.locale(code);
            const lang = loc.nativeLanguageName || "";
            const country = loc.nativeCountryName || "";
            if (lang && country && country !== lang) return `${lang} (${country})`;
            if (lang) return lang;
        } catch (e) {}
        return code;
    }

    component TranslationScanner: Process {
        id: translationScanner
        required property string translationsDir
        signal languagesScanned(var languages, var names)

        command: [
            "python3", "-c",
            `import glob, json, os, sys
langs = {}
for path in glob.glob(os.path.join(sys.argv[1], '*.json')):
    fname = os.path.basename(path)
    if fname.startswith('quotes_'): continue
    code = os.path.splitext(fname)[0]
    name = code
    try:
        data = json.load(open(path, encoding='utf-8'))
        name = data.get('Language Name', code)
    except Exception:
        pass
    langs[code] = name
for code in sorted(langs):
    print(code + '|' + langs[code])`,
            translationScanner.translationsDir
        ]
        running: true

        stdout: StdioCollector {
            id: languagesCollector
            onStreamFinished: {
                const output = languagesCollector.text;
                const langs = {};
                if (output.trim().length > 0) {
                    const lines = output.trim().split('\n').map(f => f.trim()).filter(f => f.length > 0);
                    for (const line of lines) {
                        const sep = line.indexOf('|');
                        if (sep < 0) continue;
                        const code = line.substring(0, sep);
                        const name = line.substring(sep + 1);
                        if (code.length > 0) langs[code] = name.length > 0 ? name : code;
                    }
                }
                if (Object.keys(langs).length > 0) {
                    translationScanner.languagesScanned(Object.keys(langs), langs);
                    return;
                }
                translationScanner.languagesScanned([], {});
            }
        }

        onExited: (exitCode, exitStatus) => {
            if (exitCode !== 0) {
                translationScanner.languagesScanned([], {});
            }
        }
    }

    component TranslationReader: FileView {
        id: translationReader
        property string translationsDir: ""
        property string languageCode: ""
        signal contentLoaded(var data)

        function reread() { // Proper reload in case the file was incorrect before
            if (!translationReader.translationsDir || !translationReader.languageCode) {
                translationReader.path = "";
                return;
            }
            translationReader.path = "";
            translationReader.path = `${translationReader.translationsDir}/${translationReader.languageCode}.json`;
            translationReader.reload();
        }
        path: ""

        onLoaded: {
            var textContent = "";
            try {
                textContent = text();
                var jsonData = JSON.parse(textContent);
                translationReader.contentLoaded(jsonData);
            } catch (e) {
                console.log("[I18nService] Failed to load translations:", e);
                translationReader.contentLoaded({});
            }
        }
        onLoadFailed: error => {
            translationReader.contentLoaded({});
        }
    }
}
