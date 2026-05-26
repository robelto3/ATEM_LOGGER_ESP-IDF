# Popis projektu

## Název projektu

`ATEM_LOGER_ESP-IDF`

## Platforma

- ESP-IDF v6.0.1
- Target: `esp32p4`
- Deska: ESP32-P4-ETH
- Starší ESP32-P4 revize v1.3

V `sdkconfig` musí být nastaveno:

```text
Select ESP32-P4 revisions <3.0
Minimum Supported ESP32-P4 Revision: Rev v1.0
```

Nepoužívat `force flash`, pokud není jasné proč.

## Účel projektu

Projekt slouží jako ATEM logger pro záznam střihů do EDL souboru.

Hlavní funkce:

- příjem ATEM UDP komunikace
- čtení ATEM Program / Preview
- čtení LTC timecode
- převod LTC 25 fps na TCx2 pro 50p / EDL
- zápis střihů do CMX EDL souboru na SD kartu
- webové rozhraní pro zobrazení, stažení a mazání souborů
- OLED stavový displej
- UART/debug konzole
- tally výstupy pro Program a Preview
- uložené názvy pořadů pro EDL TITLE
- rozdělení úloh mezi dvě jádra ESP32-P4

## Aktuální funkční stav

- ATEM parser čte `PrgI` a `PrvI`.
- Program i Preview se ukládají do `app_state`.
- Změna ATEM Program busu vytváří EDL event.
- První PGM po připojení ATEMu se bere jen jako výchozí synchronizace a nevytváří falešný CUT.
- LTC vstup funguje na GPIO4.
- LTC valid timeout je cca 500 ms.
- Po odpojení TC má OLED ukázat `LTC:---`.
- Fake cut tlačítko funguje na GPIO46.
- Nový EDL soubor se vytváří tlačítkem na GPIO5.
- GPIO5 i GPIO46 jdou přes `logger_events` queue.
- SD zápis funguje.
- Web funguje.
- OLED funguje.
- UART IP/debug funguje.
- Tally výstupy fungují i pro fake cut test bez ATEMu.
- EDL TITLE používá aktivní název pořadu bez číslování; pořadové číslo zůstává jen ve fyzickém názvu souboru.
- Logger queue při zátěžovém testu zůstává bez zahozených událostí.

## Způsob práce na projektu

Projekt je veden modulárně:

- každý komponent má vlastní `.c` a `.h`
- `main` má zůstat co nejkratší
- nezasahovat zbytečně do funkčních částí
- postupovat po malých krocích
- po každém kroku musí být projekt kompilovatelný
- změny dokumentovat v této složce `docs`
