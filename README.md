**Sessy Controller**

- **Purpose:** Lightweight ESP-IDF app that provides a touchscreen UI to monitor and control a Sessy battery/dongle. Implements mDNS discovery, Sessy HTTP API integration, and an LVGL-based UI with dedicated Auto Laden and Settings controls.

**Features**
- **mDNS discovery:** only accepts devices advertising TXT field `device = "Sessy Dongle"` (see `main/wifi/wifi_manager.c`).
- **Sessy API integration:** status, strategy, and energy polling; immediate control from the UI via `sessy_api_*` calls and `sessy_poll_now()` for synchronous refresh.
- **LVGL UI:** screens for Status, Control, Energy, Settings and an Auto Laden page with a large toggle button.
- **Runtime-configurable settings:** WiFi credentials, Sessy hostname & API credentials, and optional Sessy Idle behavior at 0% SOC—all configurable from the Settings UI without recompiling.

**Quick Build & Flash**
- Setup ESP-IDF as usual (see esp-idf docs). From workspace root:

```bash
# build
idf.py build

# flash (set your port and baud in sdkconfig or use the VSCode tasks)
idf.py -p COM3 flash

# monitor serial
idf.py -p COM3 monitor
```

**Configuration**
- Run `idf.py menuconfig` (or use the SDK Configuration Editor in VS Code) and open the *Sessy Controller Configuration* menu.
- `SESSY_IDLE_AT_SOC_ZERO` — when enabled, the Auto Laden button is considered active (green) when the Sessy SOC is 0%, in addition to when the active strategy is `IDLE`. This option can be toggled at runtime in the Settings UI (no recompile needed). Default is configurable in `main/Kconfig.projbuild`.
- Common options also live in `main/Kconfig.projbuild` (WiFi defaults, polling intervals).

**Settings UI**
- **WiFi Credentials:** SSID and Password fields side-by-side (48% width each).
- **Sessy Hostname:** Optional; leave blank to use mDNS discovery.
- **Sessy API Credentials:** Username and Password fields side-by-side (from the sticker on the Sessy device).
- **Features:** Toggle for "Treat SOC==0% as Sessy Idle" (enables the SOC==0% condition for Auto Laden button).
- **SAVE/RESET buttons:** SAVE persists changes to NVS flash; RESET restores factory defaults.

**Auto Laden UI behavior**
- The Auto Laden screen displays a large toggle button:
  - When strategy == `IDLE` OR (if enabled) SOC == 0%: button is *active* (green) with label `Auto Laden`.
  - Otherwise: button is *inactive* (red) with label `Auto niet laden`.
  - Pressing the button toggles between `IDLE` and `NOM` strategies and refreshes status immediately.

**Notes for contributors**
- UI code lives under `main/ui/` (screens: `ui_main.c`, `ui_status.c`, `ui_strategy.c`, `ui_energy.c`, `ui_settings.c`, `ui_auto_load.c`).
- Settings are stored in NVS flash via `main/settings/settings.c`. All `settings_set_*()` functions persist to flash automatically.
- If you change Kconfig entries, update `main/Kconfig.projbuild`. Defaults are loaded via `settings_init()` → `load_defaults()` (uses Kconfig) → `load_from_nvs()` (overrides with saved values).
- UI layout uses LVGL flex containers. Side-by-side fields use `LV_FLEX_FLOW_ROW` with 48% width columns; full-width fields use `LV_PCT(100)`.
- Keep LVGL API usage consistent with the library version in `components/lvgl__lvgl`.

**Troubleshooting**
- If a compile error references `LV_OPA_25` / `LV_OPA_35`, replace with supported constants (e.g., `LV_OPA_20`, `LV_OPA_30`) matching the LVGL version used.
- If UI looks incorrect, run a clean build and verify `sdkconfig` and `sdkconfig.defaults` are set as expected.

---
Project maintained in this workspace. If you want I can add a short developer guide (how the polling task, shared data and UI timers interact).
