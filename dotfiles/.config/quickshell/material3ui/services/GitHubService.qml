pragma Singleton

import "../core"
import QtQuick
import Quickshell
import Quickshell.Io

Singleton {
    id: root

    property bool loading: false
    property string errorMessage: ""

    property string username: (Config.ready && Config.options.github) ? Config.options.github.githubUsername : ""
    property string token: (Config.ready && Config.options.github) ? Config.options.github.githubToken : ""

    property var user: ({
        login: "",
        name: "",
        avatarUrl: "",
        bio: "",
        followers: 0,
        following: 0,
        publicRepos: 0
    })

    property var latestRepo: ({
        name: "",
        language: "",
        stars: 0,
        description: ""
    })

    property int totalStars: 0
    property int totalForks: 0
    property int totalContributions: 0

    // ── Contribution heatmap data ──
    // activity: { "YYYY-MM-DD" -> contribution count }
    property var activity: ({})
    // contributionGrid: array of weeks (Sun-first), each an array of 7 ints 0..4, 53 weeks ending today
    property var contributionGrid: []
    // contributionDays: flat array of raw counts, index 0 = gridStartDate
    property var contributionDays: []
    // gridStartDate: "YYYY-MM-DD" of the Sunday that contributionGrid/contributionDays start on
    property string gridStartDate: ""
    property int streak: 0 // current consecutive day streak

    property int weekCount: 53

    onUsernameChanged: refresh()
    onTokenChanged: refresh()

    Connections {
        target: Config
        function onReadyChanged() {
            if (Config.ready) root.refresh()
        }
    }

    // ── Request handling (curl via Process, avoids Qt's QSslSocket/HTTP2 warning) ──
    property int session: 0
    property int pending: 0

    property string profileBody: ""
    property string reposBody: ""
    property string contribBody: ""

    function baseCurl() {
        var args = ["curl", "--http1.1", "-s", "--fail-with-body", "-H", "Accept: application/vnd.github+json"]
        if (root.token !== "") {
            args.push("-H", "Authorization: Bearer " + root.token)
        }
        return args
    }

    // ── Data processing helpers ──
    function zeroPad(n) { return n < 10 ? "0" + n : "" + n }

    function fmtDate(d) {
        return d.getFullYear() + "-" + zeroPad(d.getMonth() + 1) + "-" + zeroPad(d.getDate())
    }

    function intensity(count) {
        if (count <= 0) return 0
        if (count <= 2) return 1
        if (count <= 4) return 2
        if (count <= 7) return 3
        return 4
    }

    // Build a Sun-first aligned grid of `weekCount` weeks ending today from the activity map.
    function buildContributionGrid() {
        var today = new Date()
        today.setHours(0, 0, 0, 0)

        var dow = today.getDay() // 0=Sun .. 6=Sat
        var thisWeekStart = new Date(today)
        thisWeekStart.setDate(today.getDate() - dow)

        var start = new Date(thisWeekStart)
        start.setDate(thisWeekStart.getDate() - ((weekCount - 1) * 7))

        var grid = []
        var cur = new Date(start)
        for (var w = 0; w < weekCount; w++) {
            var week = []
            for (var d = 0; d < 7; d++) {
                week.push(intensity(root.activity[fmtDate(cur)] || 0))
                cur.setDate(cur.getDate() + 1)
            }
            grid.push(week)
        }
        root.contributionGrid = grid
        root.gridStartDate = fmtDate(start)

        var days = []
        var cur2 = new Date(start)
        for (var i = 0; i < (weekCount * 7); i++) {
            days.push(root.activity[fmtDate(cur2)] || 0)
            cur2.setDate(cur2.getDate() + 1)
        }
        root.contributionDays = days

        root.computeStreak()
    }

    function computeStreak() {
        var streak = 0
        var scan = new Date()
        scan.setHours(0, 0, 0, 0)
        var maxBack = 370
        while (maxBack > 0 && (root.activity[fmtDate(scan)] || 0) > 0) {
            streak++
            scan.setDate(scan.getDate() - 1)
            maxBack--
        }
        root.streak = streak
    }

    function clearHeatmap() {
        root.totalContributions = 0
        root.activity = {}
        root.contributionGrid = []
        root.contributionDays = []
        root.gridStartDate = ""
        root.streak = 0
    }

    function applyUser(d) {
        root.user = {
            login: d.login || root.username,
            name: d.name || d.login || root.username,
            avatarUrl: d.avatar_url || "",
            bio: d.bio || "",
            followers: d.followers || 0,
            following: d.following || 0,
            publicRepos: (d.public_repos || 0) + (d.owned_private_repos || 0)
        }
    }

    function setHttpError() {
        var body = (root.profileBody || root.reposBody || root.contribBody || "")
        try {
            var err = JSON.parse(body)
            root.errorMessage = err.message || "HTTP error"
        } catch (e) {
            root.errorMessage = "HTTP error"
        }
    }

    // ── Requests ──
    Process {
        id: profileProc
        property int mySession: -1

        stdout: StdioCollector {
            onStreamFinished: {
                root.profileBody = this.text
            }
        }

        onRunningChanged: {
            if (running) {
                mySession = root.session
                root.pending++
                root.profileBody = ""
            }
        }

        onExited: (code) => {
            if (mySession !== root.session) return
            root.pending--
            if (root.pending <= 0) root.loading = false

            if (code === 0) {
                try {
                    var d = JSON.parse(root.profileBody)
                    if (d && d.login) {
                        root.applyUser(d)
                        root.errorMessage = ""
                    } else {
                        root.errorMessage = d.message || "User not found"
                    }
                } catch (e) {
                    root.errorMessage = "Parse error"
                }
            } else if (code === 22) {
                root.setHttpError()
            } else {
                root.errorMessage = "No network"
            }
        }
    }

    Process {
        id: reposProc
        property int mySession: -1

        stdout: StdioCollector {
            onStreamFinished: {
                root.reposBody = this.text
            }
        }

        onRunningChanged: {
            if (running) {
                mySession = root.session
                root.pending++
                root.reposBody = ""
            }
        }

        onExited: (code) => {
            if (mySession !== root.session) return
            root.pending--
            if (root.pending <= 0) root.loading = false

            if (code === 0) {
                try {
                    var arr = JSON.parse(root.reposBody)
                    if (Array.isArray(arr)) {
                        var stars = 0
                        var forks = 0
                        for (var i = 0; i < arr.length; i++) {
                            stars += arr[i].stargazers_count || 0
                            forks += arr[i].forks_count || 0
                        }
                        root.totalStars = stars
                        root.totalForks = forks
                        if (arr.length > 0) {
                            var lr = arr[0]
                            root.latestRepo = {
                                name: lr.name || "",
                                language: lr.language || "",
                                stars: lr.stargazers_count || 0,
                                description: lr.description || ""
                            }
                        }
                    } else {
                        root.errorMessage = arr.message || "Parse error"
                    }
                } catch (e) {
                    root.errorMessage = "Parse error"
                }
            } else if (code === 22) {
                root.setHttpError()
            } else {
                root.errorMessage = "No network"
            }
        }
    }

    Process {
        id: contribProc
        property int mySession: -1

        stdout: StdioCollector {
            onStreamFinished: {
                root.contribBody = this.text
            }
        }

        onRunningChanged: {
            if (running) {
                mySession = root.session
                root.pending++
                root.contribBody = ""
            }
        }

        onExited: (code) => {
            if (mySession !== root.session) return
            root.pending--
            if (root.pending <= 0) root.loading = false

            if (code === 0) {
                try {
                    var data = JSON.parse(root.contribBody)
                    if (data.errors && data.errors.length > 0) {
                        root.errorMessage = data.errors[0].message
                        root.clearHeatmap()
                        console.log("[GitHubService] GraphQL errors:", JSON.stringify(data.errors))
                        return
                    }
                    var u = data.data && data.data.user
                    if (!u) {
                        root.errorMessage = "User not found"
                        root.clearHeatmap()
                        return
                    }
                    var cal = u.contributionsCollection.contributionCalendar
                    root.totalContributions = cal.totalContributions || 0
                    var weeks = cal.weeks || []
                    var map = {}
                    for (var i = 0; i < weeks.length; i++) {
                        var days = weeks[i].contributionDays || []
                        for (var d = 0; d < days.length; d++) {
                            var day = days[d]
                            if (!day || !day.date) continue
                            map[day.date] = day.contributionCount || 0
                        }
                    }
                    root.activity = map
                    root.buildContributionGrid()
                    root.errorMessage = ""
                } catch (e) {
                    root.errorMessage = "Parse error"
                }
            } else if (code === 22) {
                root.setHttpError()
                root.clearHeatmap()
            } else {
                root.errorMessage = "No network"
                root.clearHeatmap()
            }
        }
    }

    function fetchProfile() {
        var endpoint = root.token !== ""
            ? "https://api.github.com/user"
            : "https://api.github.com/users/" + encodeURIComponent(root.username)
        profileProc.command = root.baseCurl().concat([endpoint])
        profileProc.running = true
    }

    function fetchRepos() {
        var url = "https://api.github.com/users/" + encodeURIComponent(root.username) + "/repos?per_page=100&sort=updated"
        reposProc.command = root.baseCurl().concat([url])
        reposProc.running = true
    }

    function fetchContributions() {
        var to = new Date()
        to.setHours(23, 59, 59, 0)
        var from = new Date(to)
        from.setDate(from.getDate() - 364)
        from.setHours(0, 0, 0, 0)

        var query = [
            "query($login: String!, $from: DateTime!, $to: DateTime!) {",
            "  user(login: $login) {",
            "    contributionsCollection(from: $from, to: $to) {",
            "      contributionCalendar {",
            "        totalContributions",
            "        weeks {",
            "          contributionDays {",
            "            contributionCount",
            "            date",
            "          }",
            "        }",
            "      }",
            "    }",
            "  }",
            "}"
        ].join("")

        var body = JSON.stringify({
            query: query,
            variables: { login: root.username, from: from.toISOString(), to: to.toISOString() }
        })

        var args = root.baseCurl()
        args.push("-H", "Content-Type: application/json")
        args.push("-X", "POST")
        args.push("-d", body)
        args.push("https://api.github.com/graphql")
        contribProc.command = args
        contribProc.running = true
    }

    function doRefresh() {
        root.session++
        root.pending = 0
        root.errorMessage = ""

        if (!root.username) {
            root.user = {
                login: "",
                name: "",
                avatarUrl: "",
                bio: "",
                followers: 0,
                following: 0,
                publicRepos: 0
            }
            root.latestRepo = { name: "", language: "", stars: 0, description: "" }
            root.totalStars = 0
            root.totalForks = 0
            root.clearHeatmap()
            root.errorMessage = "No username"
            root.loading = false
            return
        }

        root.loading = true
        root.fetchProfile()
        root.fetchRepos()
        root.fetchContributions()
    }

    Timer {
        id: debounceTimer
        interval: 150
        running: false
        repeat: false
        onTriggered: root.doRefresh()
    }

    function refresh() {
        debounceTimer.restart()
    }

    readonly property bool widgetEnabled: Config.ready
        && Config.options.appearance && Config.options.appearance.githubWidget
        && Config.options.appearance.githubWidget.showOnDesktop

    Timer {
        id: autoRefreshTimer
        interval: 30 * 60 * 1000
        running: root.widgetEnabled
        repeat: true
        triggeredOnStart: true
        onTriggered: root.refresh()
    }
}