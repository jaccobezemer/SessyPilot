# Sessy Controller

Lightweight ESP-IDF app that provides a touchscreen UI to monitor and control a Sessy battery/dongle. Implements mDNS discovery, Sessy HTTP API integration, and an LVGL-based UI with dedicated Auto Laden and Settings controls.

## Hardware

- ESP32-S3 with 480x480 RGB LCD touchscreen (ST7701S display + GT911 touch controller)
- TCA9554PWR I/O expander for touch reset pin
- PSRAM for LCD frame buffers (double-buffered)

## Features

- **mDNS discovery:** only accepts devices advertising TXT field `device = "Sessy Dongle"` (see `main/wifi/wifi_manager.c`).
- **Sessy API integration:** status, strategy, and energy polling; immediate control from the UI via `sessy_api_*` calls and `sessy_poll_now()` for synchronous refresh.
- **LVGL UI:** screens for Status, Control, Energy, Settings and an Auto Laden page with a large toggle button.
- **OTA firmware update:** HTTP server on port 8080 accepts firmware uploads with progress overlay on the display.
- **Runtime-configurable settings:** WiFi credentials, Sessy hostname & API credentials, and optional Sessy Idle behavior at 0% SOC—all configurable from the Settings UI without recompiling.
- **Status bar:** Shows WiFi status, IP address (center), and Sessy connection status at the top of the screen.

## Quick Build & Flash

Setup ESP-IDF 5.5.1 as usual (see esp-idf docs). From workspace root:

```bash
# build
idf.py build

# flash (set your port and baud in sdkconfig or use the VSCode tasks)
idf.py -p COM3 flash

# monitor serial
idf.py -p COM3 monitor

# OTA update (while connected to same WiFi)
curl -X POST http://<device-ip>:8080/ota --data-binary @build/Sessy-Controller.bin
```

## Architecture

- **LVGL task** (app_main, Core 0) — UI rendering with 10ms tick, 1s refresh timer
- **Sessy polling task** (Core 1, 8KB stack) — HTTP polling via esp_http_client + cJSON
- **Shared data** (`app_shared_data_t` in `main/app_data.h`) protected by mutex between tasks
- LVGL is NOT thread-safe: widgets are only updated from LVGL timer callbacks
- I2C master bus shared between GT911 touch and TCA9554PWR I/O expander

## Display Configuration

- PCLK: 14 MHz (reduced from 18 MHz for PSRAM bandwidth stability)
- Bounce buffer: 10 lines (reduces SPI0 contention between LCD DMA and PSRAM)
- Double frame buffer in PSRAM
- `pclk_active_neg = false` (correct for this ST7701S panel)

## Configuration

- Run `idf.py menuconfig` (or use the SDK Configuration Editor in VS Code) and open the *Sessy Controller Configuration* menu.
- `SESSY_IDLE_AT_SOC_ZERO` — when enabled, the Auto Laden button is considered active (green) when the Sessy SOC is 0%, in addition to when the active strategy is `IDLE`. This option can be toggled at runtime in the Settings UI (no recompile needed). Default is configurable in `main/Kconfig.projbuild`.
- Common options also live in `main/Kconfig.projbuild` (WiFi defaults, polling intervals).

## Settings UI

- **WiFi Credentials:** SSID and Password fields side-by-side (48% width each).
- **Sessy Hostname:** Optional; leave blank to use mDNS discovery.
- **Sessy API Credentials:** Username and Password fields side-by-side (from the sticker on the Sessy device).
- **Features:** Toggle for "Treat SOC==0% as Sessy Idle" (enables the SOC==0% condition for Auto Laden button).
- **SAVE/RESET buttons:** SAVE persists changes to NVS flash; RESET restores factory defaults.

## Auto Laden UI behavior

- The Auto Laden screen displays a large toggle button:
  - When strategy == `IDLE` OR (if enabled) SOC == 0%: button is *active* (green) with label `Auto Laden`.
  - Otherwise: button is *inactive* (red) with label `Auto niet laden`.
  - Pressing the button toggles between `IDLE` and `NOM` strategies and refreshes status immediately.

## Dependencies

- ESP-IDF 5.5.1
- LVGL 8.4.x (via ESP-IDF component manager, `~8.4.0`)
- espressif/mdns ^1.0.0

## Project Structure

```text
main/
├── main.c              # Hardware init, WiFi/UI/polling startup
├── app_data.h          # Shared data struct between tasks
├── Kconfig.projbuild   # Menuconfig options
├── ota/                # OTA HTTP server with progress tracking
├── sessy/              # Sessy REST API client + polling task
├── settings/           # NVS storage for WiFi/Sessy config
├── wifi/               # WiFi STA + mDNS discovery
├── ui/                 # LVGL screens
│   ├── ui_main.c       # Status bar, tabview, OTA overlay, refresh timer
│   ├── ui_auto_load.c  # Auto Laden toggle screen
│   ├── ui_status.c     # Status display
│   ├── ui_strategy.c   # Strategy control
│   ├── ui_energy.c     # Energy display
│   └── ui_settings.c   # Settings form
├── Touch/GT911.c       # Touch controller driver (I2C master API)
└── TCA9554PWR/         # I/O expander driver (I2C master API)
```

## Notes for contributors

- All code is C (no C++). LVGL callbacks must be plain C function pointers.
- Settings are stored in NVS flash via `main/settings/settings.c`. All `settings_set_*()` functions persist to flash automatically.
- If you change Kconfig entries, update `main/Kconfig.projbuild`. Defaults are loaded via `settings_init()` → `load_defaults()` (uses Kconfig) → `load_from_nvs()` (overrides with saved values).
- UI layout uses LVGL flex containers. Side-by-side fields use `LV_FLEX_FLOW_ROW` with 48% width columns; full-width fields use `LV_PCT(100)`.
- I2C uses the new `driver/i2c_master.h` API (not the legacy `driver/i2c.h`).
- Sessy API convention: positive power = generating/discharging, negative = charging.

## Troubleshooting

- **Display drift/horizontal shift:** Ensure PCLK is 14 MHz or lower, bounce buffer is enabled, and `CONFIG_EXAMPLE_DOUBLE_FB` is set.
- **Stack overflow on touch:** Main task stack must be at least 8192 bytes (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`).
- If UI looks incorrect, run a clean build and verify `sdkconfig` and `sdkconfig.defaults` are set as expected.
