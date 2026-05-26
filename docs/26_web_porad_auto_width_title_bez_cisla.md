# Web: Pořad automatická šířka a TITLE bez čísla

Úprava po kroku se sloupcem `Pořad`:

- sloupec `Pořad` ve výpisu souborů už nemá pevnou šířku,
- šířka se přizpůsobuje délce názvu pořadu,
- nové EDL soubory zapisují `TITLE` pouze jako aktivní název pořadu, bez číslování,
- pořadové číslo konkrétní session zůstává ve fyzickém názvu souboru `DDMMRRNN.edl`,
- při čtení starších EDL se pro webový sloupec `Pořad` skryje starší suffix typu `(004)` nebo `004`.

Příklad nového EDL:

```text
*CREATED: 09.05.2026 13:30:00
TITLE: Ranní vysílání
FCM: NON-DROP FRAME
```
