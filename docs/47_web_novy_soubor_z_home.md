# Web: Uzavřít a vytvořit nový EDL z Home

Na hlavní stránce **Home** je vedle položky `Aktuální soubor` nové tlačítko:

```text
Uzavřít a vytvořit nový
```

Tlačítko slouží jako webová obdoba fyzického tlačítka na GPIO5.

## Chování

Po kliknutí se zobrazí potvrzovací dotaz:

```text
Opravdu uzavřít aktuální EDL soubor a vytvořit nový?
```

Po potvrzení web odešle požadavek na vytvoření nového EDL souboru přes `logger_events` frontu.

## Důležité

Web handler nezapisuje na SD kartu přímo. Používá stejnou frontu jako fyzické tlačítko:

```c
logger_events_submit_new_file();
```

Tím zůstává zachované bezpečné pořadí událostí:

```text
čekající CUT eventy → uzavření aktuální session → vytvoření nového EDL
```

Nově vytvořený EDL soubor se dál automaticky chrání proti smazání podle stávající logiky.
