# Web Home – klikací hlavička ATEM Logger

Na stránce **Home** je hlavní nadpis `ATEM Logger` nově klikací.

Kliknutí na nadpis vede na `/`, takže funguje jako refresh hlavní stránky.

Zároveň bylo odstraněno spodní tlačítko `Refresh`, protože jeho funkci přebírá právě klikací hlavička.

CSS:

```css
.home-title {
    color: #eee;
    text-decoration: none;
}

.home-title:hover {
    color: #7cc7ff;
    text-decoration: none;
}
```

Funkční části loggeru se tím nemění.
