# Home: klikací datum/čas pro RTC synchronizaci

Na stránce Home je řádek s RTC datem a časem klikací.

Chování:

- datum a čas jsou zobrazené jako první řádek pod nadpisem `Home`,
- mezi datem a časem je větší mezera pro lepší čitelnost,
- kliknutí na datum/čas vyvolá JavaScript potvrzení,
- potvrzení ukáže nový čas z prohlížeče,
- po potvrzení se zavolá stávající RTC synchro endpoint `/rtc_sync`,
- synchronizace tedy používá stejnou logiku jako tlačítko `RTC Synchro`.

Příklad potvrzovacího dotazu:

```text
Opravdu synchronizovat RTC z času prohlížeče?

Nový čas: 12.05.2026 19:45:10
```

Webové živé obnovování přes `/api/state` zůstává zachované.
