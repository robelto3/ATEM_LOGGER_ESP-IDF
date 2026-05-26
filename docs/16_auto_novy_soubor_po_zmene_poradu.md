# Automatický nový EDL soubor po změně aktivního pořadu

## Cíl

Když se na webové stránce `Názvy pořadů` vybere jiný aktivní pořad, logger automaticky založí nový EDL soubor.

Důvod:

- aby se další střih omylem nezapsal do souboru se starým TITLE,
- aby změna pořadu okamžitě oddělila novou EDL session,
- aby fyzický název souboru zůstal krátký a spolehlivý.

## Chování

Po uložení stránky `Názvy pořadů` se porovná původní aktivní pořad a nový aktivní pořad.

Nový EDL soubor se založí, když se změní:

- aktivní slot pořadu,
- nebo text aktivního názvu pořadu.

Pokud se upraví jen neaktivní uložené názvy, aktuální EDL soubor zůstane beze změny.

## Zpracování přes logger frontu

Požadavek na nový soubor nejde přímo z web handleru do SD zápisu. Vkládá se přes:

```c
logger_events_submit_new_file();
```

Výhoda:

- zachová se pořadí vůči předchozím CUT eventům,
- pomalý SD zápis zůstává v logger tasku,
- web server přímo neřeší EDL zápis.

## Výsledek

Příklad původního aktivního pořadu:

```text
TITLE: Ranní vysílání
```

Po výběru pořadu `Koncert` se automaticky vytvoří nový EDL soubor a jeho hlavička bude například:

```text
TITLE: Koncert
```

Fyzický název souboru zůstává dál ve formátu:

```text
DDMMRRNN.edl
```
