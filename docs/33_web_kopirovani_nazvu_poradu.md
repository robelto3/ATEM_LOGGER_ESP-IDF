# Kopírování názvu pořadu ze seznamu souborů

Ve výpisu souborů je ve sloupci `Pořad` název pořadu klikací.

Chování:

- při najetí myší se zobrazí nápověda `Zkopírovat`
- po kliknutí se název pořadu zkopíruje do schránky prohlížeče
- po úspěšném kopírování se text krátce zvýrazní zeleně a tooltip se změní na `Zkopírováno`

Použité CSS třídy:

```css
.copy-title
.copy-title:hover
.copy-title.copied
```

Kopírování je řešené JavaScriptem přes `navigator.clipboard.writeText()` s jednoduchým fallbackem přes dočasné `textarea`.


## Úprava step 35

Tečkované podtržení bylo odstraněno. Tooltip je zkrácený na `Zkopírovat`.
