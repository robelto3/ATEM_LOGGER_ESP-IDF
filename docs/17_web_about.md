# Web About stránka

Do webového rozhraní byla přidána stránka:

```text
/about
```

Na Home stránce je nové tlačítko:

```text
About
```

## Účel

Stránka slouží jako stručný popis projektu přímo v zařízení. Hodí se při předání, kontrole nebo rychlém připomenutí, co logger umí.

## Obsah stránky

Stránka About obsahuje:

- název projektu `ATEM_LOGER_ESP-IDF`
- stručný popis zařízení
- výpis hlavních funkcí
- informaci o rozdělení tasků mezi Core 0 a Core 1
- důležité použité piny

## Hlavní body

Na stránce je uvedeno, že logger umí:

- číst ATEM Program / Preview přes UDP
- zapisovat CUT eventy do CMX EDL souboru
- číst LTC 25 fps a používat TCx2
- vytvářet EDL soubory na SD kartě
- používat uložené názvy pořadů pro `TITLE: Název pořadu`
- zobrazovat, stahovat a mazat soubory přes web
- synchronizovat RTC z času prohlížeče
- zobrazovat stav na OLED
- ovládat Program / Preview tally výstupy
- testovat střihy přes fake cut GPIO46
- běžet rozděleně na dvě jádra ESP32-P4

## Poznámka

Stránka About je statická. Nezasahuje do loggeru, SD zápisu, ATEM komunikace ani EDL logiky.
