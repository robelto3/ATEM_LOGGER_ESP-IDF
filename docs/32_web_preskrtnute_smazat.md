# Přeškrtnuté neaktivní mazání

Ve výpisu souborů jsou neaktivní mazací volby u chráněných souborů zobrazené šedě a přeškrtnutě.

Platí pro:

- `smazat` u chráněných neaktuálních souborů
- `smazat aktuální` u chráněného aktuálního souboru

Použitá CSS třída:

```css
.disabled-delete {
    color: #aaa;
    text-decoration: line-through;
}
```

Funkce ochrany proti smazání se tím nemění, jde pouze o vizuální zvýraznění, že volba není aktivní.
