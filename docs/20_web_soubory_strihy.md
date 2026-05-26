# Výpis souborů – sloupec Střihy a klikací název

Na stránce **Soubory na SD kartě** je v tabulce nový sloupec:

```text
Střihy
```

Tabulka má nově pořadí sloupců:

```text
|  | Soubor | Pořad | Střihy | Velikost | Zobrazit | Stáhnout | Chráněno | Smazat |
```

## Počet střihů

Počet střihů se počítá přímo z EDL souboru při zobrazení stránky `/files`.

Počítají se řádky, které začínají šestimístným číslem EDL eventu, například:

```text
000001  CAM1     V     C ...
000002  CAM2     V     C ...
```

Počet se počítá pouze pro soubory na aktuálně zobrazené stránce seznamu, tedy maximálně pro 20 souborů.

Výhody:

- není potřeba žádné další ukládání,
- výsledek odpovídá skutečnému obsahu EDL,
- prázdné soubory se spočítají rychle,
- při větším počtu souborů se neprochází celý seznam, jen aktuální stránka.

U souborů, které nejsou `.edl`, se ve sloupci zobrazí pomlčka.

## Klikací název souboru

Název souboru ve sloupci `Soubor` je nyní zároveň odkaz na zobrazení souboru.

To znamená, že soubor lze otevřít dvěma způsoby:

- kliknutím na název souboru,
- kliknutím na odkaz `zobrazit`.

## Zarovnání sloupců

Sloupce `Soubor`, `Střihy` a `Velikost` mají upravené odsazení tak, aby mezery mezi nimi působily vyrovnaněji.

Sloupec `Soubor` má užší pevnější šířku. Sloupec `Střihy` je zarovnaný doprava a má užší pevnější šířku, aby lépe navazoval na sloupec `Velikost`.


## Sloupec Pořad

Sloupec `Pořad` zobrazuje hodnotu z řádku `TITLE:` v EDL souboru. Detail je popsaný v `25_web_sloupec_porad.md`.
