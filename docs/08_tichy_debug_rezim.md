# Tichý UART/debug režim

## Cíl

Výchozí stav po startu má být tichý:

- debug je po startu vypnutý
- UART nevypisuje úvodní hlášku
- UART nevypisuje trvalý prompt
- UART neechuje psané znaky
- běžné stavové a diagnostické výpisy neběží
- EDL se dál zapisuje na SD kartu

UART odpoví pouze po zadání příkazu, například:

```text
help
ip
debug status
debug on
debug off
reboot
```

## Ovládání

Zapnutí debug výpisů:

```text
debug on
```

nebo:

```text
debug
```

Vypnutí debug výpisů:

```text
debug off
```

Zjištění stavu:

```text
debug status
```

## Co bylo upraveno

- `debug_control` při startu nastaví debug na OFF.
- `debug_control` zároveň vypíná ESP-IDF logy přes `esp_log_level_set("*", ESP_LOG_NONE)`.
- Při `debug on` se ESP-IDF logy povolí na úroveň INFO.
- `serial_console` je tichá, dokud nedostane příkaz.
- `edl_writer` zapisuje EDL do souboru i při vypnutém debug režimu, ale nevypisuje EDL řádky do UARTu.
- Startovací výpisy SD/LTC/session jsou podmíněné debug režimem.

## Poznámka

Některé úplně rané ROM hlášky ESP čipu po resetu nemusí jít potlačit aplikačním kódem. Projekt ale po startu aplikace drží UART tichý.
