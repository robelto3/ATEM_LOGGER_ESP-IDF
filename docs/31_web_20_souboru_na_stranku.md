# Web – 20 souborů na stránku

Výpis souborů na stránce `/files` nově zobrazuje maximálně 20 záznamů na jednu stránku.

Důvody:

- přehlednější tabulka,
- méně čtení z SD karty při zobrazení stránky,
- sloupce `Pořad` a `Střihy` se počítají jen pro aktuálně zobrazené soubory,
- stránkování zůstává zachované.

Nastavení je v souboru:

```text
components/web_server/web_server.c
```

Konstanta:

```c
#define WEB_FILES_PER_PAGE     20
```
