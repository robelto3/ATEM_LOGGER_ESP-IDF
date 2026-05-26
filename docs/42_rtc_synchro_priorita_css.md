# RTC Synchro – priorita CSS

Tlačítko `RTC Synchro` je HTML `button`, zatímco okolní tlačítka jsou většinou odkazy.

Aby obecný styl `button.btn` nepřebil speciální vzhled RTC tlačítka, používá se konkrétnější CSS pravidlo:

```css
button.btn.rtc-sync-btn {
    color: #7cc7ff;
    font-weight: bold;
    font-size: 16px;
    font-family: inherit;
    line-height: normal;
}
```

Ostatní formulářová tlačítka používají `button.btn` a mají světlé čitelné písmo.
