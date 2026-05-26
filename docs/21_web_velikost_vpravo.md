# Web – sloupec Velikost zarovnaný doprava

V tabulce souborů na stránce `/files` je sloupec `Velikost` zarovnaný doprava.

Tím se velikost souborů opticky srovná se sloupcem `Střihy`, který je také zarovnaný doprava.

Upravená CSS třída:

```css
.size-cell {
    text-align: right;
    white-space: nowrap;
    width: 95px;
    padding-left: 10px;
    padding-right: 10px;
}
```
