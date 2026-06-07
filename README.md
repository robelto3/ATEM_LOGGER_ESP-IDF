# ATEM Logger ESP-IDF

Projekt pro záznam střihů z ATEM switcheru do EDL souborů s využitím ESP32-P4-ETH.

Cílem je vytvořit samostatný logger, který sleduje změny Program vstupu na ATEMu, čte LTC timecode, ukládá střihové události na SD kartu a umožňuje základní ovládání a kontrolu přes webové rozhraní a OLED displej.

## Hardware

Projekt je určený pro desku:

* ESP32-P4-ETH
* SD karta přes SDMMC
* OLED displej
* RTC modul
* vstup LTC timecode
* připojení k ATEM switcheru přes Ethernet
* tlačítko pro ukončení aktuální EDL session
* testovací tlačítko pro fake cut bez připojeného ATEMu

## Výchozí síťové nastavení

Výchozí IP adresy používané při vývoji:

* ESP32-P4 logger: `10.0.0.9`
* ATEM switcher: `10.0.0.10`

IP adresy je možné upravovat podle aktuální konfigurace sítě.

## Funkce projektu

* připojení k ATEM switcheru
* sledování Program / Preview vstupů
* záznam změn Program vstupu jako střihových událostí
* čtení LTC timecode
* převod LTC 25 fps na výstupní TC x2 pro 50p workflow
* ukládání EDL souborů na SD kartu
* automatické vytvoření nového EDL souboru po startu
* ukončení aktuální session tlačítkem
* testování střihů bez ATEMu pomocí fake cut vstupu
* zobrazení stavu na OLED displeji
* základní webové rozhraní

## EDL výstup

Výstupní EDL je určený pro další použití například v DaVinci Resolve.

Použitý styl:

* CMX
* NON-DROP FRAME
* číslované eventy
* název kamery podle ATEM vstupu
* source in/out a record in/out z LTC timecode

Příklad struktury:

```text
*CREATED: DD.MM.RRRR HH:MM:SS
TITLE: DDMMRRNN
FCM: NON-DROP FRAME

000001 CAM7 V C 09:17:46:48 09:19:11:48 09:17:46:48 09:19:11:48
*FROM CLIP NAME: CAM7
*SOURCE FILE: CAM7
```

## SD karta

EDL soubory se ukládají na SD kartu.

Název souboru je tvořen podle data a pořadového čísla:

```text
DDMMRRNN.edl
```

Příklad:

```text
25042601.edl
```

Číslování se nezaplňuje zpětně. Nový soubor dostane vždy číslo o 1 vyšší než nejvyšší existující soubor pro daný den.

## Tlačítka a vstupy

Aktuálně používané vstupy:

* tlačítko pro ukončení aktuální EDL session
* fake cut tlačítko pro testování bez fyzického ATEMu

Po stisku tlačítka pro ukončení session se aktuální EDL soubor uzavře a vytvoří se nový.

## Vývojové prostředí

Projekt je vytvořený pro:

* ESP-IDF
* VS Code
* ESP-IDF Extension
* ESP32-P4

Běžný pracovní postup:

1. otevřít projekt ve VS Code
2. upravit zdrojové soubory
3. provést build
4. nahrát firmware do ESP32-P4
5. zkontrolovat výpis v monitoru
6. po ověření provést commit a push na GitHub

## Git

Do Git repozitáře patří hlavně:

* zdrojové soubory
* komponenty
* `CMakeLists.txt`
* `sdkconfig`
* dokumentace
* README

Do Git repozitáře nepatří:

* `build/`
* binární výstupy
* dočasné soubory
* cache
* lokální uživatelská nastavení IDE

## Poznámky

Projekt je vyvíjený postupně a modulárně. Jednotlivé části mají být oddělené tak, aby bylo možné snadno ladit displej, SD kartu, RTC, LTC, ATEM komunikaci, webové rozhraní a samotný EDL záznam.

Před většími úpravami je vhodné nejdřív vytvořit commit funkční verze.
