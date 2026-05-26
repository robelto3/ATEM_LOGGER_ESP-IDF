# 58 - Home: větší písmo a odstraněné tlačítko RTC Synchro

Úprava Home stránky:

- tlačítko `RTC Synchro` bylo z Home odstraněno,
- synchronizace RTC zůstává dostupná kliknutím na datum/čas nahoře,
- texty na Home jsou zvětšené přes třídu `home-card`,
- tlačítka na Home zůstávají v původní velikosti.

Použité CSS:

```css
.home-card {
    font-size: 18px;
    line-height: 1.35;
}

.home-card h2 {
    font-size: 26px;
    margin-top: 0;
}

.home-card .btn {
    font-size: 14px;
    line-height: normal;
}
```

Funkce RTC synchronizace se nemění. Pouze se už nespouští samostatným tlačítkem na Home, ale kliknutím na datum/čas.
