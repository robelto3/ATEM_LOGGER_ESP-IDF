# Výpis souborů – sloupec Pořad

Na stránce **Soubory na SD kartě** je přidaný sloupec:

```text
Pořad
```

Tabulka má nově pořadí:

```text
|  | Soubor | Pořad | Střihy | Velikost | Zobrazit | Stáhnout | Chráněno | Smazat |
```

## Jak se název pořadu zjišťuje

Při zobrazení stránky `/files` se pro aktuálně zobrazené soubory čte začátek/obsah EDL souboru a hledá se řádek:

```text
TITLE: Název pořadu
```

Do sloupce `Pořad` se zobrazí text za `TITLE:`.

Příklad:

```text
TITLE: Ranní vysílání
```

Ve sloupci `Pořad` se zobrazí:

```text
Ranní vysílání
```

## Výkon

Čtení souboru se využívá společně s počítáním střihů, takže se soubor neotevírá zvlášť podruhé.

Počet střihů i název pořadu se počítají pouze pro aktuálně zobrazenou stránku seznamu, tedy maximálně pro 20 souborů.

U prázdných souborů nebo souborů bez řádku `TITLE:` se zobrazí pomlčka.
