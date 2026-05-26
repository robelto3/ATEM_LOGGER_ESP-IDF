# RTC Synchro – sjednocení fontu

Tlačítko `RTC Synchro` je HTML `button`, zatímco ostatní položky jsou odkazy `a.btn`.
Aby se písmo nevykreslovalo jinak, má nyní speciální pravidlo:

```css
.rtc-sync-btn{color:#7cc7ff;font-weight:bold;font-size:16px;font-family:inherit;}
button.rtc-sync-btn{font-family:inherit;line-height:normal;}
```

Barva, tučnost a velikost zůstávají jen pro RTC Synchro. Ostatní tlačítka zůstávají beze změny.
