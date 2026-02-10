**Sessy Controller**

- **Purpose:** Lightweight ESP-IDF app that provides a touchscreen UI to monitor and control a Sessy battery/dongle. Implements mDNS discovery, Sessy HTTP API integration, and an LVGL-based UI with a dedicated "Auto Laden" control.

**Features**
- **mDNS discovery:** only accepts devices advertising TXT field `device = "Sessy Dongle"` (see `main/wifi/wifi_manager.c`).
- **Sessy API integration:** status, strategy, and energy polling; immediate control from the UI via `sessy_api_*` calls and `sessy_poll_now()` for synchronous refresh.
- **LVGL UI:** screens for Status, Control, Energy, Settings and an Auto Laden page with a large toggle button.

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
- New option: `AUTOLOAD_SOC_ZERO_ACTIVE` — when enabled the Auto Laden button is considered active (green) when the Sessy SOC is 0%, in addition to when the active strategy is `IDLE`. Default is configurable in `main/Kconfig.projbuild`.
- Common options also live in `main/Kconfig.projbuild` (WiFi defaults, polling intervals).

**Auto Laden UI behavior**
- The Auto Laden screen shows a large toggle button:
  - When strategy == `IDLE` OR (if enabled) SOC == 0% the button is *active* (green) and label shows `Auto Laden`.
  - Otherwise the button is *inactive* (red) and label shows `Auto niet laden`.
  - Pressing the button immediately calls `sessy_api_set_strategy()` and then `sessy_poll_now()` to refresh status.

**Notes for contributors**
- UI code lives under `main/ui/` (screens: `ui_main.c`, `ui_status.c`, `ui_strategy.c`, `ui_energy.c`, `ui_settings.c`, `ui_auto_load.c`).
- If you change Kconfig entries, update `main/Kconfig.projbuild` and document defaults.
- Keep LVGL API usage consistent with the library version in `components/lvgl__lvgl`.

**Troubleshooting**
- If a compile error references `LV_OPA_25` / `LV_OPA_35`, replace with supported constants (e.g., `LV_OPA_20`, `LV_OPA_30`) matching the LVGL version used.
- If UI looks incorrect, run a clean build and verify `sdkconfig` and `sdkconfig.defaults` are set as expected.

---
Project maintained in this workspace. If you want I can add a short developer guide (how the polling task, shared data and UI timers interact).
