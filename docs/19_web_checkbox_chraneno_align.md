# Web: vystředění checkboxu Chráněno

## Core step 18

Ve výpisu souborů je checkbox ve sloupci `Chráněno` vystředěný.

Mezera mezi sloupcem `Chráněno` a sloupcem `Smazat` je zmenšená, aby tabulka působila kompaktněji.

Použité CSS třídy:

```css
.protect-cell {
    text-align: center;
    padding-left: 4px;
    padding-right: 4px;
}

.delete-cell {
    padding-left: 4px;
}
```

Funkce ochrany souboru se nemění:

- chráněný soubor nejde smazat,
- při odškrtnutí ochrany se zobrazí potvrzení,
- ochrana je uložená v NVS podle názvu souboru.
