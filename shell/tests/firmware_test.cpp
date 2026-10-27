// fwupdmgr's JSON: devices with their updates, the sources, release notes
#include "services/Firmware.h"

#include <cstdio>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// As fwupd 2 writes them (get-updates: the devices with their releases)
static const char* kUpdates = R"json({
  "Devices" : [
    {
      "Name" : "System Firmware",
      "DeviceId" : "a45df35ac0e948ee180fe216a5f703f32dda163f",
      "Summary" : "UEFI ESRT device",
      "Plugin" : "uefi_capsule",
      "Flags" : ["internal", "updatable", "require-ac", "supported", "registered", "needs-reboot"],
      "Problems" : ["system-power-too-low", "require-ac-power"],
      "Vendor" : "LENOVO",
      "Version" : "0.1.47",
      "Icons" : ["computer"],
      "UpdateState" : "needs-reboot",
      "Releases" : [
        {
          "AppstreamId" : "com.lenovo.ThinkPadN2HET.firmware",
          "RemoteId" : "lvfs",
          "Summary" : "Lenovo ThinkPad X1 Carbon System Firmware",
          "Description" : "<p>This stable release fixes:</p><ul><li>Battery &amp; charging</li><li>Fan noise</li></ul>",
          "Version" : "0.1.48",
          "Size" : 12058624,
          "Urgency" : "high",
          "Flags" : ["trusted-payload", "trusted-metadata", "is-upgrade"]
        },
        {
          "Version" : "0.1.46",
          "Flags" : ["is-downgrade"]
        }
      ]
    },
    {
      "Name" : "Touchpad",
      "DeviceId" : "6e6a0ec2f87cbbbc6a5a1ef1e79f4b8c2f6fe1f4",
      "Plugin" : "synaptics_rmi",
      "Flags" : ["updatable", "needs-reboot"],
      "Releases" : [ { "Version" : "1.3", "Flags" : ["trusted-metadata"] } ],
      "Version" : "1.3",
      "UpdateState" : 2,
      "Icons" : ["input-touchpad"]
    },
    { "Name" : "No id" },
    { "DeviceId" : "0123", "Plugin" : "linux_display" }
  ]
})json";

static const char* kRemotes = R"json({
  "Remotes" : [
    { "Id" : "lvfs", "Kind" : "download", "Title" : "Linux Vendor Firmware Service", "Flags" : ["enabled", "automatic-security-reports"],
      "ModificationTime" : 1793000000 },
    { "Id" : "lvfs-testing", "Kind" : "download", "Title" : "Linux Vendor Firmware Service (testing)", "Flags" : [] },
    { "Id" : "vendor", "Kind" : "local", "Enabled" : "true" },
    { "Id" : "lvfs-2", "Kind" : "download", "Flags" : 1, "Enabled" : true, "ModificationTime" : null, "Mtime" : 1793110161 },
    { "Id" : "lvfs-embargo", "Kind" : "download", "Flags" : 128, "Enabled" : false, "Mtime" : -1 }
  ]
})json";

int main() {
    const auto devices = Firmware::parseDevices(kUpdates);
    expect(devices.size() == 2, "two devices (one without an id, one without a name, skipped)");
    if (devices.size() == 2) {
        const auto& sys = devices[0];
        expect(sys.name == "System Firmware" && sys.vendor == "LENOVO" && sys.version == "0.1.47", "the device");
        expect(sys.updatable() && sys.has("require-ac") && sys.needsReboot() && sys.appliesOnRestart(), "its flags");
        expect(sys.problems.size() == 2 && Firmware::problemText(sys.problems[1]) == "Plug in the charger", "its problems");
        expect(sys.symbol() == "computer", "its symbol");
        expect(sys.releases.size() == 2, "its releases");
        if (sys.releases.size() == 2) {
            const auto& r = sys.releases[0];
            expect(r.version == "0.1.48" && r.urgency == "high" && r.remote == "lvfs" && r.size == 12058624 && r.upgrade, "a release");
            expect(r.description == "This stable release fixes:\n• Battery & charging\n• Fan noise", "its notes as text");
            expect(!sys.releases[1].upgrade, "a downgrade");
        }
        expect(devices[1].updateState == "success" && !devices[1].needsReboot(), "a numeric state (fwupd 2)");
        expect(devices[1].appliesOnRestart(), "the needs-reboot flag: updates apply on restart, none pending");
        expect(devices[1].releases.size() == 1 && !devices[1].releases[0].upgrade, "the release it runs isn't an update");
        expect(devices[1].symbol() == "touchpad_mouse", "the touchpad's symbol");
    }
    expect(Firmware::parseDevices("").empty() && Firmware::parseDevices("{}").empty() && Firmware::parseDevices("not json").empty(),
           "nothing to read");

    const auto remotes = Firmware::parseRemotes(kRemotes);
    expect(remotes.size() == 5, "five sources");
    if (remotes.size() == 5) {
        expect(remotes[3].enabled && remotes[3].modified == 1793110161, "fwupd 2: Enabled and Mtime");
        expect(!remotes[4].enabled && remotes[4].modified == 0, "never downloaded");
        expect(remotes[0].enabled && remotes[0].modified == 1793000000 && remotes[0].kind == "download", "the LVFS, on");
        expect(!remotes[1].enabled, "testing, off");
        expect(remotes[2].enabled && remotes[2].title == "vendor", "an older Enabled value, and a title from the id");
    }

    expect(Firmware::plainText("<p>One\n  line</p>\n<p>Two</p>") == "One line\nTwo", "paragraphs");
    expect(Firmware::plainText("Plain &lt;text&gt;") == "Plain <text>", "entities");

    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
