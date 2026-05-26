# Web CSS – Bobova doladěná verze

Tento krok sjednocuje webové styly podle Bobovy aktuální ručně doladěné verze.

Upraveno hlavně:

- šířky a odsazení sloupců v tabulce souborů
- sloupec `Pořad` bez pevné šířky, ale s větším odsazením
- `Střihy` a `Velikost` zarovnané doprava
- `Zobrazit` a `Stáhnout` zarovnané na střed
- `Chráněno` vystředěné a s větším odsazením od předchozích sloupců
- `Smazat` s menším, ale jasným odsazením

CSS je v souboru:

```text
components/web_server/web_server.c
```

Hledat blok začínající:

```c
"<style>"
```
