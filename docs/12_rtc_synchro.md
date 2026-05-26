# RTC synchro

## Cíl

RTC DS3231 lze synchronizovat z webového prohlížeče.

Na hlavní stránce je tlačítko:

```text
RTC synchro
```

Po kliknutí se vezme lokální čas zařízení, na kterém běží prohlížeč, a pošle se loggeru.

## Web endpoint

Používá se endpoint:

```text
/rtc_sync
```

Parametry:

```text
y   = rok, například 2026
mo  = měsíc 1–12
d   = den 1–31
h   = hodina 0–23
mi  = minuta 0–59
s   = sekunda 0–59
dow = den v týdnu 1–7
```

Příklad:

```text
/rtc_sync?y=2026&mo=5&d=8&h=19&mi=30&s=12&dow=5
```

## Chování

Handler zavolá:

```c
rtc_set_datetime(&dt);
app_state_update_rtc(&dt, true);
```

Takže po úspěšném zápisu se nový RTC čas hned zobrazí i na webu.

## Poznámka

Čas je převzatý z prohlížeče. Před synchronizací je tedy dobré mít správný čas na počítači nebo telefonu, ze kterého web otevíráš.
