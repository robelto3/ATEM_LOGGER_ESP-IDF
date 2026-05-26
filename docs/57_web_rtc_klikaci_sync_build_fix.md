# 57 - Build fix pro klikací RTC datum/čas

Oprava kompilace po přidání klikacího RTC data/času na Home.

Dlouhý HTML řádek pro klikací RTC datum/čas se nevešel do lokálního bufferu `line[192]`, takže GCC při `-Werror=format-truncation` zastavil build.

Řešení:

- dlouhý RTC řádek se neposílá přes jedno `snprintf()`,
- HTML se posílá po menších částech přes `web_send_chunk()`,
- přes `snprintf()` se skládá jen samotné datum a samotný čas.

Funkčně se nic nemění:

- datum a čas jsou dál nahoře pod Home,
- mezi datem a časem je větší mezera,
- kliknutí na datum/čas vyvolá potvrzení RTC synchronizace,
- v potvrzení se ukáže nový čas z prohlížeče.
