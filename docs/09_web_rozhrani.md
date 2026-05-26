# Webové rozhraní

## Mazání jednoho souboru

Po potvrzení mazání jednoho souboru přes web se po úspěšném smazání prohlížeč automaticky vrátí na seznam souborů:

```text
/files
```

Platí pro:

- běžný EDL soubor
- aktuální EDL soubor

U aktuálního EDL souboru zůstává zachované původní chování:

1. uzavře se případný rozpracovaný segment,
2. RAM session se vynuluje,
3. aktuální soubor se smaže,
4. vytvoří se nový aktuální EDL soubor,
5. web se vrátí na seznam souborů.

Pokud se mazání nepovede, zůstane zachovaná chybová stránka s odkazem zpět na seznam souborů.

## Hromadné mazání

Hromadné mazání přes checkboxy zatím ponechává výsledkovou stránku se souhrnem:

```text
Smazáno / přeskočeno / chyba
```

Důvod: u hromadného mazání je užitečné vidět, co se skutečně smazalo a co bylo přeskočeno.


## Názvy pořadů

Na hlavní stránce je tlačítko `Názvy pořadů`.

Stránka `/shows` umožňuje uložit až 5 názvů pořadů a vybrat aktivní název.

Uložené názvy a aktivní výběr jsou v NVS. Čítač TITLE se neukládá, ale dopočítává se z EDL souborů na SD.

Při změně aktivního pořadu se automaticky založí nový EDL soubor přes logger frontu, aby se další střih nezapsal do předchozího pořadu.


## RTC synchro

Na hlavní stránce je tlačítko `RTC synchro`.

Po kliknutí prohlížeč přes JavaScript vezme svůj lokální čas a pošle ho loggeru na endpoint:

```text
/rtc_sync?y=2026&mo=5&d=8&h=19&mi=30&s=12&dow=5
```

Logger potom zapíše čas do DS3231 přes `rtc_set_datetime()` a zároveň obnoví `app_state`, aby web hned ukázal nový RTC čas.

Poznámka: synchronizuje se čas podle zařízení, ze kterého je otevřený webový prohlížeč.


## Home stránka

Na hlavní stránce se nadpis stavové karty zobrazuje jako:

```text
Home
```

Aktivní pořad je na Home stránce zvýrazněný barvou `#ff8a8a`, aby byl hned vidět při kontrole před natáčením.

Navigační odkazy zpět na hlavní stránku používají text:

```text
Home
```

## Názvy pořadů – rozložení výběru

Na stránce `Názvy pořadů` je výběr aktivního pořadu umístěný vpravo za textovým polem názvu. Radio puntík je zvýrazněný barvou `crimson`.

## About stránka

Na Home stránce je tlačítko `About`.

Endpoint:

```text
/about
```

Stránka About stručně popisuje, co logger je a jaké má hlavní funkce. Obsahuje také přehled rozdělení jader a důležité piny.

Je pouze informační a nezasahuje do běhu loggeru.

## Ochrana souborů proti smazání

Ve výpisu souborů je u každého řádku checkbox `chránit`.

- zaškrtnutý soubor je chráněný proti smazání
- stav se ukládá do NVS ESP32-P4
- chráněný soubor nejde smazat samostatně ani hromadně
- při odškrtnutí ochrany se zobrazí potvrzení


## Core step 17 – sloupec Chráněno v tabulce souborů

Ve výpisu souborů je sloupec `Chráněno` umístěný mezi akcemi `Stáhnout` a `Smazat`.

Checkbox už nemá text `chránit`; stav je zřejmý ze sloupce `Chráněno` a z toho, že u chráněného souboru je místo odkazu na smazání text:

```text
chráněno proti smazání
```


## Výpis souborů – počet střihů

Tabulka souborů má pořadí sloupců:

```text
|  | Soubor | Střihy | Velikost | Zobrazit | Stáhnout | Chráněno | Smazat |
```

Název souboru ve sloupci `Soubor` je zároveň klikací odkaz na zobrazení souboru.

Sloupec `Střihy` se počítá přímo z EDL souborů při zobrazení seznamu. Počítají se pouze soubory na aktuálně zobrazené stránce, tedy maximálně 20 souborů.

U souborů, které nejsou `.edl`, se zobrazí pomlčka.

## Výpis souborů - režimy zobrazení

Výchozí stránka `/files` po resetu ukazuje jen soubory, které mají nenulový počet střihů, plus aktuální EDL soubor.

Dostupné režimy:

- `Zobrazit jen soubory se střihy` → `/files?mode=cuts`
- `Zobrazit jen střihy = 0` → `/files?mode=empty`
- `Zobrazit všechny soubory` → `/files?mode=all`

Vybraný režim se pamatuje jen v RAM do resetu. Aktuální EDL soubor se zobrazuje vždy, i když neodpovídá právě zvolenému filtru.

## Home: Uzavřít a vytvořit nový EDL

Na stránce Home je vedle aktuálního souboru tlačítko `Uzavřít a vytvořit nový`.
Po potvrzení odešle požadavek přes `logger_events_submit_new_file()`, tedy stejně jako fyzické tlačítko na GPIO5.
Web tím nezapisuje na SD přímo, pouze vloží událost do logger fronty.


## Preview Tally

Na stránce **Nastavení** je checkbox **Preview Tally**. Zaškrtnutý stav zapíná zelené PVW tally výstupy, odškrtnutý stav je vypne. Nastavení se ukládá do NVS.
