# OLED startovací IP obrazovka

Při startu se na OLED krátce zobrazí obě důležité IP adresy:

```text
ATEM LOGGER
START IP
ESP  10.0.0.9
ATEM 10.0.0.10
```

Význam:

- `ESP` = IP adresa ESP32-P4 / web serveru
- `ATEM` = IP adresa ATEM switcheru

Obrazovka drží cca 3 sekundy.

Během této doby může aplikace pokračovat ve startu. Hlavní OLED obrazovka se zobrazí až po uplynutí startovacího zobrazení.

IP adresy se berou z `net_config`, tedy z hodnot uložených v NVS nebo z výchozích hodnot:

```text
ESP/server: 10.0.0.9
ATEM:       10.0.0.10
```
