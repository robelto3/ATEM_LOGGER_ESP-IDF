# Výběr všech zobrazených souborů ke smazání

Ve výpisu souborů je v hlavičce prvního sloupce přidaný checkbox pro výběr všech aktuálně zobrazených mazatelných souborů.

## Chování

- funguje ve všech třech režimech výpisu:
  - soubory se střihy
  - soubory se Střihy = 0
  - všechny soubory
- vybírá pouze soubory na aktuální stránce výpisu
- chráněné soubory zůstávají nevybratelné
- aktuální soubor zůstává nevybratelný pro hromadné mazání
- tlačítko `Smazat vybrané` se aktivuje až po vybrání alespoň jednoho mazatelného souboru
- limit hromadného mazání zůstává max. 20 souborů

## Technická poznámka

Checkbox v hlavičce používá ID:

```html
selectVisibleFiles
```

Mazatelné soubory v řádcích zůstávají jako:

```html
input.filecheck:not(:disabled)
```

JavaScript při změně hlavičkového checkboxu zaškrtne nebo odškrtne všechny právě zobrazené mazatelné řádky.
