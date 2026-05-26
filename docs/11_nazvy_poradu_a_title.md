# Názvy pořadů a EDL TITLE

## Cíl

Fyzický název EDL souboru zůstává krátký a spolehlivý:

```text
DDMMRRNN.edl
```

Například:

```text
08052601.edl
```

Uvnitř EDL souboru je ale lidsky čitelný název pořadu:

```text
TITLE: Ranní vysílání
```

## Uložené názvy pořadů

V NVS se ukládá až 5 názvů pořadů a aktivní výběr.

Příklad:

```text
show 1 = Ranní vysílání
show 2 = Studio Host
show 3 = Koncert
show 4 = Jednorázová akce
show 5 =
active = show 1
```

Nastavení je dostupné přes web:

```text
/nazvy pořadů = /shows
```

Na hlavní stránce je tlačítko:

```text
Názvy pořadů
```

## Čítač pořadu

Pořadové číslo názvu pořadu se už nepoužívá v TITLE.

Při vytvoření nového EDL se do hlavičky zapíše pouze aktivní název pořadu:

```text
TITLE: Ranní vysílání
```

Pořadové číslo je jen ve fyzickém názvu souboru `DDMMRRNN.edl`.

Například:

```text
TITLE: Ranní vysílání
TITLE: Koncert
TITLE: Ranní vysílání
TITLE: Koncert
```

## Výhody

- název pořadu se nepřepisuje při každém resetu
- čítač pořadu se nepřepisuje do NVS
- fyzické číslování souborů zůstává podle dne a pořadí souboru
- fyzický název souboru zůstává krátký `8.3`

## Komponent

Nastavení názvů řeší komponent:

```text
components/show_config/
    show_config.c
    include/show_config.h
    CMakeLists.txt
```

Důležité konstanty:

```c
#define SHOW_CONFIG_SLOT_COUNT      5
#define SHOW_CONFIG_NAME_MAX_LEN    64
#define SHOW_CONFIG_DEFAULT_NAME    "ATEM LOGGER"
```

## Kdy se změna projeví

Při změně aktivního pořadu přes web se automaticky vloží požadavek na nový EDL soubor přes logger frontu.

Díky tomu se další střih už zapisuje do nového souboru s novým TITLE.

Už existující EDL soubory se nepřepisují.
