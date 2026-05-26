# Web: RTC Synchro bez ovlivnění ostatních tlačítek

Úprava po kroku 46.

Cíl:

- ostatní odkazy/tlačítka ponechat v původním stylu webu
- speciální modrá/tučná/větší úprava se týká pouze tlačítka `RTC Synchro`

Důvod:

Předchozí úprava nastavila `color`, `font-size` a další vlastnosti přímo na obecnou třídu `.btn`, takže se změnil vzhled všech tlačítek.

Nově:

```css
.btn {
    display: inline-block;
    background: #2c2c2c;
    border: 1px solid #555;
    border-radius: 8px;
    padding: 8px 12px;
    margin: 4px 8px 4px 0;
}

.rtc-sync-btn {
    color: #7cc7ff;
    font-weight: bold;
    font-size: 16px;
    font-family: Arial, sans-serif;
}
```

Tím zůstávají ostatní tlačítka ve stejném stylu jako dřív a `RTC Synchro` je zvýrazněné samostatně.
