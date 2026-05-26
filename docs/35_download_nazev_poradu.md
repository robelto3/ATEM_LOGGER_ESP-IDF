# Stažení EDL pod názvem pořadu

Fyzický název souboru na SD kartě zůstává krátký a bezpečný:

```text
DDMMRRNN.edl
```

Při stažení přes web se ale prohlížeči nabídne název podle řádku `TITLE:` v EDL souboru:

```text
Název pořadu.edl
```

Například soubor na SD:

```text
09052601.edl
```

se může stáhnout jako:

```text
Ranní vysílání.edl
```

Skutečný soubor na SD se nepřejmenovává.

Pokud více souborů stejného pořadu stáhneš do stejné složky, prohlížeč může podle svého chování doplnit například `(1)`, `(2)`, nebo se zeptat na přepsání.

Z názvu pro stažení se odstraňují znaky nevhodné pro názvy souborů:

```text
\ / : * ? " < > |
```
