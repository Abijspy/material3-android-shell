# System Monitor Backend (material3ui-sysmon)

`material3ui-sysmon` is Material3UI's own replacement for dgop. It lives in `scripts/sysmon/`, is pure Python 3 standard library, and reads `/proc` and `/sys` directly; the installer also links it to `~/.local/bin`.

```bash
material3ui-sysmon top                      # process table
material3ui-sysmon meta --pretty            # JSON: cpu, memory, system, processes, diskmounts, gpu
material3ui-sysmon meta --modules cpu,gpu
material3ui-sysmon server --port 63490      # GET /meta?modules=processes,system&limit=150, GET /health
```

The shell starts the server only while the System Monitor's Processes tab is open, so CPU usage per process is a true delta between samples. Without `python3` it falls back to `ps`.
