# Ochrana souboru proti smazání

Na stránce **Soubory na SD kartě** je u každého souboru checkbox ve sloupci `Chráněno`.

Checkbox je ve výpisu souborů ve sloupci `Chráněno` mezi akcemi `Stáhnout` a `Smazat`.

## Zapnutí ochrany

Po zaškrtnutí se stav uloží do NVS flash ESP32-P4.

Uživatelsky se to chová jako malá „EEPROM“ paměť v ESP32, ale technicky jde o ESP-IDF NVS.

Chráněný soubor:

- nejde vybrat pro hromadné mazání,
- nemá aktivní odkaz pro smazání,
- nejde smazat ani přímým voláním `/delete` nebo `/delete_do`.

## Vypnutí ochrany

Při odškrtnutí se zobrazí potvrzovací stránka:

```text
Opravdu zrušit ochranu proti smazání?
```

Až po potvrzení se ochrana zruší.

## Důvod

Ochrana chrání důležité EDL soubory proti nechtěnému smazání z webového rozhraní.

Fyzický soubor na SD kartě se nijak nemění. Uložený je pouze příznak ochrany v NVS podle názvu souboru.


## Core step 17 – sloupec Chráněno v tabulce souborů

Ve výpisu souborů je sloupec `Chráněno` umístěný mezi akcemi `Stáhnout` a `Smazat`.

Checkbox už nemá text `chránit`; stav je zřejmý ze sloupce `Chráněno` a z toho, že u chráněného souboru je místo odkazu na smazání text:

```text
chráněno proti smazání
```

## Automatická ochrana nových EDL souborů

Nově vytvořený EDL soubor se automaticky označí jako chráněný.

Platí pro:

- nový soubor po startu zařízení
- nový soubor po tlačítku GPIO5
- nový soubor po změně aktivního pořadu
- nový soubor vytvořený po smazání aktuálního souboru

Soubor lze stále ručně odemknout odškrtnutím checkboxu `Chráněno`. Při odemknutí zůstává potvrzovací dotaz.
