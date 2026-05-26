# Piny a hardware

## I2C

Společná I2C sběrnice:

| Funkce | GPIO |
|---|---:|
| SDA | GPIO7 |
| SCL | GPIO8 |

Zařízení na I2C:

| Zařízení | Adresa |
|---|---:|
| OLED SSD1306 128×64 | `0x3C` |
| RTC DS3231 | `0x68` |

## LTC vstup

| Funkce | GPIO |
|---|---:|
| LTC input | GPIO4 |

LTC vstup je 25 fps.

Výstupní timecode pro displej a EDL je TCx2:

- vstupní frame: 00–24
- výstupní frame: 00, 02, 04 … 48
- liché snímky se nedopočítávají

## Tlačítka

| Funkce | GPIO | Zapojení |
|---|---:|---|
| Fake cut | GPIO46 | tlačítko proti GND |
| New EDL file | GPIO5 | tlačítko proti GND, interní pull-up |

## SD slot ESP32-P4-ETH

SDMMC 4bit režim:

| Funkce | GPIO |
|---|---:|
| CLK | GPIO43 |
| CMD | GPIO44 |
| D0 | GPIO39 |
| D1 | GPIO40 |
| D2 | GPIO41 |
| D3 | GPIO42 |
| SD power | GPIO45 active LOW |

Napájení SD slotu:

- GPIO45 active LOW
- LDO VO4 channel 4
- mount path: `/sdcard`

## Ethernet

Výchozí IP adresy:

| Funkce | IP |
|---|---|
| ESP / web server | `10.0.0.9` |
| ATEM switcher | `10.0.0.10` |

ESP/server IP se dá měnit přes UART i web.

ATEM IP se mění pouze přes web.

## Tally výstupy

Tally piny jsou uložené v souboru:

```text
components/tally_outputs/include/tally_outputs_pins.h
```

### Program tally PGM 1–8

| PGM | GPIO |
|---:|---:|
| 1 | GPIO6 |
| 2 | GPIO14 |
| 3 | GPIO15 |
| 4 | GPIO16 |
| 5 | GPIO17 |
| 6 | GPIO18 |
| 7 | GPIO19 |
| 8 | GPIO54 |

### Preview tally PVW 1–8

| PVW | GPIO |
|---:|---:|
| 1 | GPIO33 |
| 2 | GPIO32 |
| 3 | GPIO27 |
| 4 | GPIO26 |
| 5 | GPIO23 |
| 6 | GPIO22 |
| 7 | GPIO21 |
| 8 | GPIO20 |

## Logika tally výstupů

V souboru `tally_outputs_pins.h`:

```c
#define TALLY_OUTPUT_ACTIVE_LEVEL   1
#define TALLY_OUTPUT_INACTIVE_LEVEL 0
```

Pro aktivní LOW zapojení přes tranzistory lze otočit:

```c
#define TALLY_OUTPUT_ACTIVE_LEVEL   0
#define TALLY_OUTPUT_INACTIVE_LEVEL 1
```

Pro domácí test bez ATEMu:

```c
#define TALLY_OUTPUT_SHOW_WITHOUT_ATEM 1
```

Význam:

- `1` = tally funguje i bez ATEMu, například při fake cut testu
- `0` = při odpojeném ATEMu se všechny tally výstupy zhasnou
