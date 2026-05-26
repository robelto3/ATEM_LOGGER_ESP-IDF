# Výchozí filtr souborů se střihy

Ve webovém výpisu souborů `/files` je po resetu výchozí zobrazení nastavené tak, že ukazuje pouze soubory, které mají nenulový počet střihů, plus aktuální EDL soubor.

Počet střihů se počítá z EDL řádků začínajících šestimístným číslem eventu, například:

```text
000001  CAM1 ...
000002  CAM2 ...
```

## Režimy výpisu

Výchozí režim:

```text
/files
/files?mode=cuts
```

Zobrazí soubory se střihy a vždy také aktuální otevřený EDL soubor, i když má zatím 0 střihů.

Režim prázdných souborů:

```text
/files?mode=empty
```

Zobrazí soubory se Střihy = 0 a vždy také aktuální otevřený EDL soubor.

Režim všech souborů:

```text
/files?mode=all
```

Zobrazí všechny soubory.

## Tlačítka na stránce

Nahoře jsou tlačítka pro přepnutí mezi režimy:

```text
Zobrazit jen soubory se střihy
Zobrazit jen střihy = 0
Zobrazit všechny soubory
```

Refresh i návrat na `/files` zachovává aktuální režim až do resetu zařízení.

## Poznámka

Kvůli filtrování je potřeba při načtení stránky přečíst EDL informace u načtených souborů. Limit seznamu je daný `WEB_MAX_FILE_LIST`, zobrazení na stránku je `WEB_FILES_PER_PAGE`.
