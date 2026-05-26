# Nový EDL soubor ze stránky Soubory na SD kartě

Na stránku **Soubory na SD kartě** bylo nahoře k ostatním tlačítkům přidáno tlačítko:

```text
Uzavřít aktuální a vytvořit nový
```

Tlačítko používá stejný webový handler `/new_file` jako tlačítko na Home stránce a stejnou logger event queue jako fyzické tlačítko na GPIO5. Web tedy přímo nezapisuje na SD kartu, pouze vloží požadavek do fronty.

Před provedením se zobrazí potvrzovací dotaz:

```text
Opravdu uzavřít aktuální EDL soubor a vytvořit nový?
```

Po potvrzení se aktuální EDL uzavře a vytvoří se nový soubor. Nový soubor se nadále automaticky chrání proti smazání.

Pokud je tlačítko použité ze stránky souborů, po vložení požadavku se web vrátí zpět na `/files`. Zvolený režim výpisu souborů zůstává zachovaný v RAM do resetu.
