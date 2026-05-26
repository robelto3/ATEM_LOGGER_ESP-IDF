# Web bez diagnostické karty

Z hlavní stránky `Home` byla odstraněna karta `Diagnostika tasků`.

Odstraněná viditelná část obsahovala:

```text
HTTP/web handler běží na core
HTTP task nastavený core
Logger queue: čeká / zahozeno
```

Důvod:

- rozdělení jader už bylo ověřené
- logger queue při zátěžových testech nezahazovala události
- hlavní stránka má být čistší pro běžné používání

Funkční části projektu se nemění:

- logger queue zůstává zachovaná
- rozdělení Core 0 / Core 1 zůstává zachované
- web, SD, OLED, tally ani EDL zápis nejsou touto změnou ovlivněné
