# Výpis souborů: zapamatování režimu do resetu

Webový výpis souborů `/files` si nově pamatuje zvolený režim pouze v RAM.

Po resetu zařízení je výchozí režim:

```text
soubory se střihy + aktuální soubor
```

To znamená:

- zobrazují se EDL soubory s počtem střihů větším než 0
- aktuální otevřený EDL soubor se zobrazí vždy, i když má zatím 0 střihů

## Přepínání režimů

Výpis souborů má tři režimy:

```text
/files?mode=cuts   → soubory se střihy + aktuální soubor
/files?mode=empty  → soubory se Střihy = 0 + aktuální soubor
/files?mode=all    → všechny soubory
```

Každý z těchto režimů se po kliknutí zapamatuje v RAM a zůstane aktivní až do resetu.

Starší odkazy `/files?all=1` a `/files?all=0` zůstávají funkční kvůli kompatibilitě.

## Chování při návratu na stránku

Když uživatel odejde na jinou stránku a potom se vrátí na `/files`, použije se poslední zvolený režim.

Po resetu se režim vrátí zpět na bezpečný výchozí filtr.
