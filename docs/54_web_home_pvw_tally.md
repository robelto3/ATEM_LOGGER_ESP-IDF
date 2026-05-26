# 54 – PVW Tally indikace na Home

Na stránce **Home** je nově nad řádkem RTC zobrazen stav zelených Preview tally výstupů:

```text
PVW Tally: ON
```

nebo:

```text
PVW Tally: OFF
```

- `ON` znamená, že zelené Preview tally výstupy jsou povolené.
- `OFF` znamená, že Preview tally výstupy jsou vypnuté a drží se zhasnuté.

Stav se bere z `net_config`, tedy ze stejného nastavení, které se ukládá do NVS na stránce **Nastavení**.

## Živý update

Endpoint `/api/state` nově vrací také položku:

```json
"pvw_tally": true
```

Home stránka ji čte v živém JavaScript update cyklu, takže změna nastavení se na Home projeví bez ručního refresh stránky.
