# Testování po úpravách

Po každé větší úpravě nejprve provést build:

```bash
source ~/.espressif/tools/activate_idf_v6.0.1.sh
idf.py build
```

Potom flash a monitor:

```bash
idf.py flash monitor
```

Nepoužívat `force flash`, pokud není jasné proč.

## Základní test

Ověřit:

1. OLED běží.
2. Ethernet je OK.
3. SD karta je OK.
4. Web je dostupný.
5. Aktuální EDL soubor existuje.
6. UART příkazy fungují.


## RTC synchro test

1. Otevřít hlavní web stránku loggeru.
2. Kliknout na `RTC synchro`.
3. Web má potvrdit nastavení RTC z času prohlížeče.
4. Po návratu na hlavní stránku má řádek RTC ukazovat nový čas.
5. Po restartu se čas DS3231 má držet dál.

## LTC test

1. Připojit LTC.
2. OLED má ukázat `LTC:OK`.
3. TCx2 má běžet.
4. Odpojit LTC.
5. Do cca 500 ms má OLED ukázat `LTC:---`.

## ATEM test

1. Připojit ATEM.
2. OLED a web mají ukázat `ATEM:OK`.
3. Program a Preview se mají aktualizovat.
4. První PGM po připojení nesmí vytvořit falešný CUT.
5. Změna PGM má vytvořit EDL event.

## Fake cut test

Bez ATEMu lze testovat přes GPIO46.

Ověřit:

1. Každý fake cut změní PGM.
2. Vytvoří se EDL event.
3. PGM tally výstupy se přepínají.
4. Web ukazuje odpovídající PGM.
5. Logger queue ukazuje `zahozeno: 0`.

## New file test

Tlačítko GPIO5:

1. Ukončí aktuální EDL session.
2. Zahodí rozpracovaný segment v RAM.
3. Vytvoří nový EDL soubor.
4. Číslování eventů začne znovu.
5. Název souboru je podle nejvyššího existujícího čísla pro daný den + 1.

## Zátěžový test

1. Stříhat rychle za sebou.
2. Současně obnovovat web.
3. Sledovat diagnostiku:

```text
Logger queue: čeká 0 / 32
zahozeno: 0
```

Pokud `zahozeno` zůstává `0`, fronta funguje správně a události se neztrácejí.
