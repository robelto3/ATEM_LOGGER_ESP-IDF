# Kopírování názvu pořadu bez podtržení

Úprava webového výpisu souborů.

## Změny

- název pořadu ve sloupci `Pořad` už nemá tečkované podtržení
- při najetí myší se zobrazí jen krátká nápověda `Zkopírovat`
- kliknutí stále zkopíruje název pořadu do schránky
- po zkopírování se text krátce zvýrazní zeleně

## CSS

```css
.copy-title {
    cursor: pointer;
}

.copy-title:hover {
    color: #fff;
}

.copy-title.copied {
    color: #7fe08a;
}
```
