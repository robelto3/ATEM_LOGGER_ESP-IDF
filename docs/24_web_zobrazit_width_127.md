# Web – šířka sloupce Zobrazit 127 px

Kosmetická úprava tabulky souborů.

Ve společném CSS ve `components/web_server/web_server.c` je třída:

```css
.view-cell {
    text-align: center;
    white-space: nowrap;
    width: 127px;
    padding-left: 8px;
    padding-right: 8px;
}
```

Tato třída se používá pro hlavičku i buňky sloupce `Zobrazit`.

Funkce webu ani práce se soubory se touto změnou nemění.
