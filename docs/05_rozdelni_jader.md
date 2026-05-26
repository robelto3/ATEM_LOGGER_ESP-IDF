# Rozdělení úloh mezi dvě jádra

Projekt běží na ESP32-P4 a používá rozdělení tasků mezi dvě jádra.

## Cíl rozdělení

Rychlé a časově citlivé části nesmí blokovat pomalé operace, jako je SD zápis nebo web.

## Core 1 – rychlá část

Na Core 1 běží:

- ATEM UDP komunikace
- ATEM parser `PrgI` / `PrvI`
- detekce změny Program busu
- LTC snapshot do `app_state`
- tlačítka GPIO46 / GPIO5
- rychlé vložení eventu do `logger_events` queue

Princip:

```text
rychlá část zjistí událost
vezme aktuální TC snapshot
vloží událost do fronty
okamžitě pokračuje dál
```

## Core 0 – pomalá/obslužná část

Na Core 0 běží:

- `logger_events` task
- SD zápis
- EDL writer
- web server
- OLED refresh
- RTC čtení
- UART/debug
- tally outputs

## Logger queue

Fronta odděluje rychlou a pomalou část.

Do fronty chodí:

- ATEM CUT
- fake cut
- new EDL file

Původní webová diagnostika fronty byla při ladění užitečná, ale po ověření stability byla z hlavní stránky odstraněna, aby byl Home čistší.

Při zátěžovém testu platilo, že správný stav je:

```text
Logger queue: zahozeno 0
```

Rozdělení jader a logger queue zůstávají v kódu zachované; odstraněná je jen viditelná diagnostická karta na Home stránce.
