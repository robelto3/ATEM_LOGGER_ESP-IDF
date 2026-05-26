# Aktuální řádek a mazání chráněného aktuálního souboru

Úprava webového seznamu souborů:

- odkazy ve sloupci `Stáhnout` jsou zelené, ale bez tučného písma
- aktuální řádek má výšku `56px`
- pokud je aktuální soubor chráněný, ve sloupci `Smazat` zůstane zachovaný text `aktuální soubor` a pod ním je šedě `smazat aktuální`
- pokud aktuální soubor chráněný není, zůstává aktivní odkaz `smazat aktuální`
- u chráněného neaktuálního souboru zůstává šedý text `smazat`

CSS:

```css
.download-cell a{color:#7fe08a;}
.current-row{height:56px;}
```
