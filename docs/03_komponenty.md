# Přehled komponent

## `i2c_bus`

Společná I2C sběrnice pro OLED a RTC.

## `ssd1306`

Nízkoúrovňový OLED driver.

- malé písmo
- zvětšený text přes scale
- kreslení textu na SSD1306 128×64

## `display`

Aplikační OLED vrstva.

Čte `app_state`.

Při startu zobrazí na cca 3 sekundy obě IP adresy:

- ESP / web server IP
- ATEM IP

Aktuální hlavní rozložení OLED:

- horní řádek vystředěný: `ATEM:OK/---    LTC:OK/---`
- PGM/PVW v jednom řádku, malé popisky a velká čísla
- velký TCx2 vystředěný
- spodní `FILE ...` vystředěné

## `ds3231`

Nízkoúrovňový RTC driver pro DS3231.

## `rtc`

Aplikační RTC vrstva.

## `ltc_input`

Nízkoúrovňový LTC dekodér.

- vstup na GPIO4
- ISR na obě hrany
- short interval cca 240–250 µs
- long interval cca 500 µs
- sync word `0x3FFD`
- valid timeout cca 500 ms

## `ltc`

Vyšší LTC vrstva nad `ltc_input`.

Poskytuje aktuální timecode přes `ltc_get_time()`.

## `app_state`

Společný stav pro OLED, web a debug.

Obsahuje například:

- ATEM connected
- LTC valid
- Program input
- Preview input
- aktuální TCx2
- aktuální soubor

## `sd_storage`

Mount SD karty přes SDMMC.

## `logger_session`

Správa EDL souborů.

Vytváří názvy:

```text
DDMMRRNN.edl
```

Pravidla:

- mezery v číslování se nevyplňují
- nový soubor má číslo nejvyšší existující pro daný den + 1
- při smazání aktuálního souboru se aktuální mazaný soubor nesmí započítat jako nejvyšší číslo

## `cut_event`

RAM logika segmentů.

- uzavírá předchozí segment
- otevírá nový segment
- používá TC snapshot z okamžiku vzniku eventu

## `edl_writer`

Zapisuje EDL na SD kartu.

Po zápisu provádí flush/fsync/close, aby byl soubor vidět přes web.

## `logger_events`

Fronta událostí.

Přes frontu jdou:

- ATEM Program změny
- fake cut GPIO46
- new file GPIO5

Fronta je příprava a předěl pro běh na dvou jádrech.

## `net_config`

IP konfigurace uložená v NVS.

- ESP/server IP lze měnit přes UART i web
- ATEM IP se mění pouze přes web

## `net_eth`

Inicializace Ethernetu.

Bere server IP z `net_config`.

## `web_server`

Webové rozhraní.

Funkce:

- hlavní stavová stránka
- zobrazení aktuálního EDL
- stažení aktuálního EDL
- seznam souborů na SD
- zobrazení, stažení, mazání souborů
- hromadné mazání přes checkboxy
- po úspěšném smazání jednoho souboru návrat rovnou na seznam `/files`
- nastavení IP
- synchronizace RTC z času prohlížeče
- refresh tlačítko na stránce aktuálního logu

Omezení:

- hromadné mazání max. 20 souborů
- aktuální soubor se nemaže hromadně, pouze samostatně
- po smazání aktuálního souboru se ihned vytvoří nový

## `serial_console`

UART příkazy a debug.

## `debug_control`

Správa debug režimu.

Debug výpisy jsou po startu vypnuté.

## `app_tasks`

Rozdělení hlavní aplikace na tasky.

- rychlá část na Core 1
- pomalá/UI část na Core 0

## `tally_outputs`

Tally výstupy pro Program a Preview.

- čte `app_state`
- ovládá PGM/PVW GPIO výstupy
- piny jsou editovatelné v `tally_outputs_pins.h`
- podporuje aktivní HIGH i aktivní LOW logiku
- podporuje fake cut test bez ATEMu


## `show_config`

Trvalé nastavení názvů pořadů v NVS.

- ukládá až 5 názvů pořadů
- ukládá aktivní výběr
- neukládá čítač pořadu
- aktivní název používá `logger_session` pro EDL TITLE

Čítač TITLE se dopočítává ze skutečných EDL souborů na SD podle řádků `TITLE:`.

## `file_protect`

Ochrana vybraných souborů proti smazání.

- stav ochrany se ukládá do NVS
- web u chráněného souboru blokuje samostatné i hromadné mazání
- při zrušení ochrany se vyžaduje potvrzení
