# ATEM Logger ESP-IDF

ATEM Logger je samostatný logger pro ESP32-P4-ETH, který sleduje Program/Preview stav ATEM switcheru, čte LTC timecode a zapisuje střihové události do CMX EDL souborů na SD kartu.

Projekt je určený pro workflow, kde referenční LTC běží na 25 fps, ale EDL se používá pro 50p střih. Logger proto ukládá čas jako TCx2: frame z LTC 25 fps se násobí dvěma a nedopočítávají se liché snímky.

## Hlavní Funkce

- čtení ATEM Program / Preview přes Ethernet a UDP parser příkazů `PrgI` a `PrvI`,
- záznam změn Program vstupu jako EDL eventy,
- čtení LTC 25 fps a převod na TCx2 pro EDL i OLED,
- nastavitelná korekce LTC ve framech původního 25fps LTC,
- automatické zakládání EDL souborů na SD kartě,
- uložené názvy pořadů a automatický `TITLE:` v EDL,
- počítání střihů od založení aktuálního souboru,
- webové rozhraní pro Home, soubory, archiv, koš, názvy pořadů, nastavení a About,
- samostatně vypínatelné Program Tally a Preview Tally,
- OLED stavová obrazovka a startovací IP obrazovka,
- RTC synchronizace z času prohlížeče,
- softwarový reboot z webu s návratem na Home a přechodem na novou IP loggeru,
- fake cut vstup pro testování bez fyzického ATEMu.

## Hardware

Projekt je stavěný pro:

- ESP32-P4-ETH,
- Ethernet připojení k ATEM switcheru,
- SD kartu přes SDMMC,
- OLED displej SSD1306 přes I2C,
- RTC DS3231 přes I2C,
- LTC vstup,
- tlačítko pro uzavření aktuální EDL session a založení nové,
- fake cut tlačítko,
- Program/Preview tally výstupy.

OLED není kritický pro start zařízení. Pokud displej chybí nebo neodpovídá, logger, Ethernet, web i SD část běží dál.

## Výchozí Síť

Výchozí adresy:

- logger / web server: `10.0.0.9`
- ATEM switcher: `10.0.0.10`
- maska: `255.255.255.0`

IP adresy se ukládají do NVS a lze je měnit na stránce `Nastavení`. Po změně IP loggeru je nejčistší provést `Reboot`; web se po restartu pokusí automaticky přejít na novou adresu loggeru.

## Webové Rozhraní

Hlavní stránky:

- `Home` - stav Ethernetu, ATEMu, LTC, SD karty, PGM/PVW, Tally, počet střihů, aktuální soubor a název pořadu,
- `Soubory na SD kartě` - výpis EDL souborů, filtrování, zobrazení obsahu, stažení, archivace a přesun do koše,
- `Archiv` - archivované soubory s možností vrácení,
- `Koš` - vrácení souborů, definitivní smazání a vyprázdnění koše,
- `Názvy pořadů` - editace až 5 uložených názvů a výběr aktivního pořadu,
- `Nastavení` - Program/Preview Tally, korekce LTC, IP loggeru a ATEMu, Reboot,
- `About` - stručný popis projektu.

Na stránce `Home` jsou hodnoty průběžně obnovované přes `/api/state`.

## SD Karta A Soubory

EDL soubory se ukládají přímo na SD kartu. Název souboru má tvar:

```text
DDMMRRNN.edl
```

Příklad:

```text
25042601.edl
```

Číslování se nezaplňuje zpětně. Nový soubor dostane číslo o 1 vyšší než nejvyšší existující soubor pro daný den.

Používané adresáře:

- `/sdcard` - běžné EDL soubory,
- `/sdcard/archive` - archiv,
- `/sdcard/trash` - koš.

Při přesunu do koše nebo při návratu z koše se řeší kolize názvů. Pokud cílový název existuje, použije se číselná přípona `.000` až `.999`. Když není volná žádná varianta, operace se odmítne jako plný koš/cíl pro daný soubor.

## EDL Výstup

Výstup je určený například pro DaVinci Resolve.

Použitý formát:

- CMX,
- NON-DROP FRAME,
- číslované eventy,
- název kamery podle ATEM vstupu,
- `TITLE:` podle aktivního názvu pořadu,
- source in/out a record in/out podle LTC/TCx2.

Příklad:

```text
*CREATED: 25.04.2026 09:17:46
TITLE: Název pořadu
FCM: NON-DROP FRAME

000001 CAM7 V C 09:17:46:48 09:19:11:48 09:17:46:48 09:19:11:48
*FROM CLIP NAME: CAM7
*SOURCE FILE: CAM7
```

## LTC Korekce

Korekce LTC se nastavuje na stránce `Nastavení`.

Hodnota se zadává ve framech vstupního LTC 25 fps:

- záporná hodnota posune logovaný timecode zpět,
- kladná hodnota posune logovaný timecode dopředu,
- povolený rozsah je `-24` až `+24`.

Protože výstupní TC je TCx2, korekce `-2` na vstupním LTC odpovídá posunu `-4` framy ve výsledném 50p EDL.

## OLED

Po startu se na OLED na 5 sekund zobrazí IP obrazovka:

```text
ATEM LOGGER
START IP
Logger 10.0.0.9
ATEM 10.0.0.10
```

Potom se zobrazí hlavní stavová obrazovka s ATEM/LTC stavem, PGM/PVW, TCx2 a počtem střihů.

## Komponenty

Projekt je rozdělený do ESP-IDF komponent:

- `app_state` - sdílený stav aplikace,
- `app_tasks` - FreeRTOS tasky pro rychlou a pomalou část,
- `atem_control` - komunikace s ATEM switcherem,
- `cut_event` - počítání střihů aktuálního souboru,
- `display` a `ssd1306` - OLED,
- `edl_writer` - zápis EDL,
- `logger_events` a `logger_session` - fronta událostí a správa aktuální session,
- `ltc` a `ltc_input` - LTC dekódování/vstup,
- `net_config` - NVS konfigurace IP, tally a LTC korekce,
- `net_eth` - Ethernet,
- `rtc` a `ds3231` - reálný čas,
- `sd_storage` - SD karta,
- `show_config` - názvy pořadů,
- `tally_outputs` - PGM/PVW tally výstupy,
- `web_server` - webové rozhraní,
- `serial_console`, `new_file_button`, `fake_cut_button` - obslužné vstupy a konzole.

## Rozdělení Úloh

- Core 1: rychlá část - ATEM, LTC snapshot, tlačítka a vkládání událostí do logger fronty.
- Core 0: pomalá/obslužná část - logger, SD zápis, web, OLED, RTC, UART a tally výstupy.

Události jdou přes `logger_events` queue, aby rychlá ATEM/LTC část nečekala na pomalý zápis na SD kartu.

## Build

Projekt je vytvořený pro ESP-IDF a ESP32-P4.

V prostředí s aktivovaným ESP-IDF lze build spustit například:

```bash
idf.py build
```

V aktuálním vývojovém prostředí se používá i přímé CMake volání:

```bash
. /home/bob/.espressif/tools/activate_idf_v6.0.1.sh
IDF_COMPONENT_MANAGER=0 cmake --build build
```

Pokud se ve VS Code build začne chovat divně, často pomůže:

1. otevřít příkazovou paletu,
2. spustit `ESP-IDF: Select ESP-IDF Version`,
3. vybrat používanou verzi ESP-IDF,
4. spustit build znovu.

## Git

Do repozitáře patří:

- zdrojové soubory,
- komponenty,
- `CMakeLists.txt`,
- `sdkconfig`,
- dokumentace,
- `README.md`.

Do repozitáře nepatří:

- `build/`,
- binární výstupy,
- cache,
- dočasné soubory,
- lokální nastavení IDE.

## Licence A Zdrojový Kód

K tomuto programu autor neuplatňuje žádná autorská práva. Program je volně použitelný, upravitelný a šiřitelný bez omezení.

Zdrojový kód je dostupný na GitHubu:

<https://github.com/robelto3/ATEM_LOGGER_ESP-IDF>

## Poznámka

Na tomto programu se mnou spolupracovali Astra (ChatGPT) a Codík (Codex), moji AI asistenti od OpenAI.
