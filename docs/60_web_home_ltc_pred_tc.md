# 60 - Home: LTC před běžícím timecode

Na stránce Home je stav `LTC: OK/---` přesunutý níže, až pod řádky `PGM` a `PVW`.

Nové pořadí části stavových hodnot:

```text
ATEM: OK/---
PGM: ...
PVW: ...
LTC: OK/---
HH:MM:SS:FF
```

U běžícího timecode je odstraněný prefix `TC:`, takže se zobrazuje jen samotná hodnota.

Živá aktualizace přes `/api/state` zůstává beze změny.
