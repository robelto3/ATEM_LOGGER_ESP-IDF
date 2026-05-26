# Výpis souborů: režim Střihy = 0

Do webového výpisu souborů byl přidán třetí režim:

```text
Zobrazit jen střihy = 0
```

Používá adresu:

```text
/files?mode=empty
```

V tomto režimu se zobrazují soubory, které mají počet střihů `0`.

Aktuální EDL soubor se zobrazuje vždy, i kdyby už měl střihy. Díky tomu je aktuální soubor stále na očích ve všech režimech výpisu.

Režim se pamatuje jen v RAM do resetu, stejně jako režim všech souborů nebo režim souborů se střihy.

Po resetu se výpis vrátí na výchozí režim:

```text
soubory se střihy + aktuální soubor
```
