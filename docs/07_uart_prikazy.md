# UART/debug příkazy

UART konzole umožňuje ovládat základní nastavení a debug.

## Příkazy

```text
help
ip
ip 192.168.1.250
debug
debug on
debug off
debug status
reboot
```

## `help`

Vypíše dostupné příkazy.

## `ip`

Bez parametru vypíše aktuální IP adresu ESP/serveru.

## `ip 192.168.1.250`

Nastaví IP adresu ESP/serveru.

Přes UART se mění pouze IP ESP/serveru.

ATEM IP se přes UART nemění.

## `debug`

Vypíše nebo přepne debug podle aktuální implementace.

## `debug on`

Zapne debug výpisy.

## `debug off`

Vypne debug výpisy.

## `debug status`

Vypíše stav debug režimu.

## `reboot`

Restartuje zařízení.

## Poznámka k IP adresám

Výchozí IP:

```text
ESP/server: 10.0.0.9
ATEM:       10.0.0.10
```

ESP IP lze měnit přes UART i web.

ATEM IP pouze přes web.
