# ATEM Logger ESP-IDF

[Česká verze](README.cs.md)

ATEM Logger is a standalone ESP32-P4-ETH based logger for Blackmagic ATEM switchers. It monitors Program/Preview state, reads LTC timecode, and writes edit events into CMX EDL files on an SD card.

The project is designed for a workflow where the reference LTC runs at 25 fps, while the resulting EDL is used for 50p editing. The logger stores timecode as TCx2: each 25 fps LTC frame is multiplied by two and missing odd frames are not generated.

## Main Features

- ATEM Program / Preview reading over Ethernet using the `PrgI` and `PrvI` UDP commands,
- logging Program bus changes as EDL events,
- LTC 25 fps input and TCx2 conversion for EDL and OLED display,
- configurable LTC correction in original 25 fps LTC frames,
- automatic EDL file creation on the SD card,
- stored show names and automatic `TITLE:` lines in EDL files,
- cut counter for the current EDL file,
- web interface for Home, files, archive, trash, show names, settings and About,
- independently switchable Program Tally and Preview Tally outputs,
- OLED status screen and startup IP screen,
- RTC synchronization from the browser time,
- software reboot from the web interface with Home redirect and automatic jump to the new logger IP,
- fake cut input for testing without a physical ATEM switcher.

## Hardware

The project is built for:

- ESP32-P4-ETH,
- Ethernet connection to an ATEM switcher,
- SD card over SDMMC,
- SSD1306 OLED display over I2C,
- DS3231 RTC over I2C,
- LTC input,
- button for closing the current EDL session and creating a new one,
- fake cut button,
- Program/Preview tally outputs.

The OLED display is not required for startup. If the display is missing or does not respond, the logger, Ethernet, web server and SD card parts continue running.

## Hardware Pin Orientation

This is a basic orientation list until a full schematic/PCB documentation is added.

Inputs and I2C:

- LTC input: `GPIO4`
- Close current file and create new EDL button: `GPIO5`, button to GND, internal pull-up
- Fake cut test button: `GPIO46`, button to GND, internal pull-up
- I2C SDA for OLED/RTC: `GPIO7`
- I2C SCL for OLED/RTC: `GPIO8`

SD card:

- CLK: `GPIO43`
- CMD: `GPIO44`
- D0: `GPIO39`
- D1: `GPIO40`
- D2: `GPIO41`
- D3: `GPIO42`
- SD power enable: `GPIO45`, active low

Ethernet RMII:

- MDC: `GPIO31`
- MDIO: `GPIO52`
- PHY reset: `GPIO51`
- RMII clock: `GPIO50`

Tally outputs:

- PGM 1-8: `GPIO6`, `GPIO14`, `GPIO15`, `GPIO16`, `GPIO17`, `GPIO18`, `GPIO19`, `GPIO54`
- PVW 1-8: `GPIO33`, `GPIO32`, `GPIO27`, `GPIO26`, `GPIO23`, `GPIO22`, `GPIO21`, `GPIO20`

Important notes:

- The LTC input needs an external input shaper/conditioner. Do not feed raw audio LTC directly into the ESP32 GPIO.
- The RTC is an external DS3231 module connected to the shared I2C bus.
- OLED and RTC share the same I2C bus.

## Default Network

Default addresses:

- logger / web server: `10.0.0.9`
- ATEM switcher: `10.0.0.10`
- netmask: `255.255.255.0`

IP addresses are stored in NVS and can be changed on the `Settings` page. After changing the logger IP, the cleanest path is to use `Reboot`; after restart, the web UI tries to switch automatically to the new logger address.

## Web Interface

Main pages:

- `Home` - Ethernet, ATEM, LTC, SD card, PGM/PVW, Tally, cut count, current file and show name,
- `Files on SD card` - EDL file list, filters, content preview, download, archive and move to trash,
- `Archive` - archived files with restore action,
- `Trash` - restore, permanent delete and empty trash,
- `Show names` - edit up to 5 stored show names and select the active one,
- `Settings` - Program/Preview Tally, LTC correction, logger and ATEM IP, Reboot,
- `About` - short project description.

The `Home` page refreshes live values through `/api/state`.

## SD Card And Files

EDL files are stored directly on the SD card. The file name format is:

```text
DDMMRRNN.edl
```

Example:

```text
25042601.edl
```

Numbering is not backfilled. A new file always gets a number one higher than the highest existing file for the same day.

Used directories:

- `/sdcard` - regular EDL files,
- `/sdcard/archive` - archive,
- `/sdcard/trash` - trash.

Name collisions are handled when moving files to trash or restoring them. If the target name already exists, a numeric suffix `.000` to `.999` is used. If no free variant is available, the operation is rejected as full for that file name.

## EDL Output

The output is intended for tools such as DaVinci Resolve.

Format:

- CMX,
- NON-DROP FRAME,
- numbered events,
- camera name based on the ATEM input,
- `TITLE:` based on the active show name,
- source in/out and record in/out based on LTC/TCx2.

Example:

```text
*CREATED: 25.04.2026 09:17:46
TITLE: Show name
FCM: NON-DROP FRAME

000001 CAM7 V C 09:17:46:48 09:19:11:48 09:17:46:48 09:19:11:48
*FROM CLIP NAME: CAM7
*SOURCE FILE: CAM7
```

## LTC Correction

LTC correction is configured on the `Settings` page.

The value is entered in original 25 fps LTC frames:

- negative values move the logged timecode backwards,
- positive values move the logged timecode forwards,
- allowed range is `-24` to `+24`.

Because the output TC is TCx2, correction `-2` on the input LTC corresponds to `-4` frames in the resulting 50p EDL.

## OLED

After startup, the OLED shows the IP screen for 5 seconds:

```text
ATEM LOGGER
START IP
Logger 10.0.0.9
ATEM 10.0.0.10
```

Then it switches to the main status screen with ATEM/LTC state, PGM/PVW, TCx2 and cut count.

## Components

The project is split into ESP-IDF components:

- `app_state` - shared application state,
- `app_tasks` - FreeRTOS tasks for fast and slow parts,
- `atem_control` - ATEM switcher communication,
- `cut_event` - current file cut counter,
- `display` and `ssd1306` - OLED,
- `edl_writer` - EDL writing,
- `logger_events` and `logger_session` - event queue and current session management,
- `ltc` and `ltc_input` - LTC decoding/input,
- `net_config` - NVS configuration for IP, tally and LTC correction,
- `net_eth` - Ethernet,
- `rtc` and `ds3231` - real-time clock,
- `sd_storage` - SD card,
- `show_config` - show names,
- `tally_outputs` - PGM/PVW tally outputs,
- `web_server` - web interface,
- `serial_console`, `new_file_button`, `fake_cut_button` - helper inputs and console.

## Task Split

- Core 1: fast path - ATEM, LTC snapshot, buttons and pushing events into the logger queue.
- Core 0: slower/service path - logger, SD writes, web, OLED, RTC, UART and tally outputs.

Events go through the `logger_events` queue so the fast ATEM/LTC part does not wait for slower SD card writes.

## Build

The project is built for ESP-IDF and ESP32-P4.

With an active ESP-IDF environment, build with:

```bash
idf.py build
```

In the current development environment, direct CMake build is also used:

```bash
. /home/bob/.espressif/tools/activate_idf_v6.0.1.sh
IDF_COMPONENT_MANAGER=0 cmake --build build
```

If the VS Code build environment starts behaving unexpectedly, this often helps:

1. open the command palette,
2. run `ESP-IDF: Select ESP-IDF Version`,
3. select the ESP-IDF version used by the project,
4. run the build again.

## Git

Files that belong in the repository:

- source files,
- components,
- `CMakeLists.txt`,
- `sdkconfig`,
- documentation,
- `README.md`.

Files that should not be committed:

- `build/`,
- binary outputs,
- cache,
- temporary files,
- local IDE settings.

## License And Source Code

The author does not claim any copyright over this program. The program is free to use, modify and redistribute without restriction.

Source code is available on GitHub:

<https://github.com/robelto3/ATEM_LOGGER_ESP-IDF>

## Note

Astra (ChatGPT) and Codík (Codex), my AI assistants from OpenAI, collaborated with me on this program.
