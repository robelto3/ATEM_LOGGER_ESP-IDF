# Web – aktivní řádek pořadu crimson

Na stránce `Názvy pořadů` je u aktivně vybraného pořadu zvýrazněný text `aktivní` barvou `crimson`.

Rozložení řádku:

```text
Název pořadu 1   [ textové pole ]   (•) aktivní
```

Použité CSS třídy:

```css
.show-active-selected {
    color: crimson;
    font-weight: bold;
}

.show-active-muted {
    color: #aaa;
}
```

Radio puntík má dál barvu `crimson` přes:

```css
input.radio {
    accent-color: crimson;
}
```
