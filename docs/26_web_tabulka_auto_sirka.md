# Web tabulka souborů – automatická šířka

Úprava vzhledu tabulky souborů na stránce `/files`.

## Problém

Po přidání sloupce `Pořad` se tabulka někdy zbytečně roztahovala přes volnou šířku stránky. Po otevření nástroje **Prozkoumat** v prohlížeči se viewport změnil a tabulka se znovu přepočítala, takže se najednou zúžila.

## Úprava

Tabulka už nemá pevně `width:100%`, ale používá:

```css
table {
    border-collapse: collapse;
    width: auto;
}
```

Sloupec `Pořad` má:

```css
.program-cell {
    white-space: nowrap;
    width: 1%;
    padding-left: 8px;
    padding-right: 10px;
}
```

To prohlížeči říká, že sloupec nemá zbytečně zabírat volné místo, ale má se držet podle obsahu.

## Obal tabulky

Tabulka je obalená prvkem:

```html
<div class="table-wrap">
```

s CSS:

```css
.table-wrap {
    overflow-x: auto;
    max-width: 100%;
}
```

Kdyby byla tabulka při dlouhých názvech širší než obrazovka, stránka se nerozbije a půjde ji vodorovně posunout.
