# Home tlačítka v hlavní kartě

Na stránce Home byla odstraněna samostatná spodní řada navigačních tlačítek.

Navigační tlačítka jsou nově přímo v hlavní kartě Home pod údajem o aktuálním souboru.

## Pořadí tlačítek

```text
Soubory na SD kartě
Názvy pořadů
RTC Synchro
Nastavení
About
Refresh
```

Původní tlačítka:

```text
Zobrazit aktuální EDL
Stáhnout aktuální EDL
```

byla z Home zrušena, protože aktuální soubor je pohodlně dostupný přes stránku `Soubory na SD kartě`.


## Sjednocení tlačítka RTC Synchro

Tlačítko `RTC Synchro` je HTML `button`, ostatní položky jsou převážně odkazy `a.btn`.
CSS třída `.btn` proto obsahuje také `font-family`, `font-size`, `line-height`, barvu textu a `cursor`, aby tlačítko i odkazy vypadaly stejně.

Pro `button.btn` je nastaveno také:

```css
appearance: none;
-webkit-appearance: none;
```
