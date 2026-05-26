# Home: LTC a běžící timecode v jednom řádku

Na stránce Home je stav LTC a běžící timecode zobrazený v jednom řádku:

```text
LTC: OK   02:55:38:20
```

nebo při neplatném LTC:

```text
LTC: ---   --:--:--:--
```

Barvy stavu LTC zůstávají stejné jako dříve:

- `OK` používá zelený stav `.ok`
- `---` používá červený stav `.bad`

Živé obnovování přes `/api/state` zůstává zachované.
