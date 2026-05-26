# Web Home – živý stav bez refresh stránky

Home stránka má živé zobrazování stavových hodnot přes malý API endpoint:

```text
/api/state
```

Endpoint vrací JSON se stavem z `app_state`:

```json
{
  "atem": true,
  "ltc": true,
  "pgm": 2,
  "pvw": 8,
  "tc": "02:06:38:20",
  "rtc_valid": true,
  "rtc": "12.05.2026 14:30:10",
  "file": "12052601.edl"
}
```

Home stránka si tento stav načítá JavaScriptem přibližně každých 250 ms a přepisuje jen hodnoty:

- ATEM OK/---
- LTC OK/---
- PGM
- PVW
- TC
- RTC
- aktuální soubor

Celá stránka se neobnovuje, takže nebliká a nezatěžuje SD kartu. Endpoint pouze čte `app_state` z RAM.

Důležité: webové živé zobrazení je orientační. Přesné EDL eventy se dál zapisují podle snapshotu v ESP, ne podle webu.
