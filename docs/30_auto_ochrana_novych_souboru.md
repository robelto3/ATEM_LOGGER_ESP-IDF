# Automatická ochrana nově vytvořených EDL souborů

Každý nově vytvořený EDL soubor se automaticky zapíše do ochrany proti smazání.

## Kdy se nový soubor automaticky chrání

- po startu zařízení
- po tlačítku GPIO5 pro nový EDL soubor
- po změně aktivního pořadu přes web
- po smazání aktuálního souboru, kdy se ihned vytvoří nový

## Chování na webu

Nový soubor má hned zaškrtnutý checkbox `Chráněno`.

Chráněný soubor:

- nejde smazat samostatně
- nejde vybrat pro hromadné mazání
- nejde smazat ani přímým URL požadavkem

Pro smazání je nutné soubor nejdřív ručně odemknout. Při odemknutí se zobrazí potvrzovací dotaz.

## Technická poznámka

Automatická ochrana se nastavuje v `logger_session_start_new_from_rtc()` po vytvoření EDL hlavičky.

Používá se:

```c
file_protect_set_protected(filename, true);
```

Stav ochrany je uložený v NVS ESP32-P4.
