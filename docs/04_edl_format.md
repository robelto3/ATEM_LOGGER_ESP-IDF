# EDL formát

## Typ

CMX / NON-DROP FRAME

Používá se pro DaVinci Resolve.

## Hlavička souboru

```text
*CREATED: DD.MM.RRRR HH:MM:SS
TITLE: <název pořadu>
FCM: NON-DROP FRAME

```

Za hlavičkou je jeden prázdný řádek.

## Název souboru

Formát:

```text
DDMMRRNN.edl
```

Příklad:

```text
07052611.edl
```

Význam:

- `07` den
- `05` měsíc
- `26` rok
- `11` pořadové číslo daného dne

Pravidla:

- nový soubor má vždy číslo nejvyšší existující pro daný den + 1
- mezery v číslování se nevyplňují
- při smazání aktuálního souboru se hned vytvoří nový

## Událost

Příklad:

```text
000001  CAM1     V     C        01:00:16:14 01:00:17:32 01:00:16:14 01:00:17:32
*FROM CLIP NAME:  CAM1
*SOURCE FILE: CAM1
```

Význam časů:

```text
source in   source out   record in   record out
```

V současné logice jsou source a record časy stejné.

## Timecode

LTC vstup je 25 fps.

Pro EDL se používá TCx2:

- frame 00 zůstane 00
- frame 01 se zapíše jako 02
- frame 02 se zapíše jako 04
- ...
- frame 24 se zapíše jako 48

Používají se jen sudé frame hodnoty:

```text
00, 02, 04, ... 48
```

Liché framy se nedopočítávají.


## TITLE podle názvu pořadu

Fyzický soubor zůstává ve formátu `DDMMRRNN.edl`, ale řádek TITLE je lidsky čitelný.

Příklad:

```text
*CREATED: 08.05.2026 18:42:10
TITLE: Ranní vysílání
FCM: NON-DROP FRAME
```

Pořadové číslo v závorce za názvem není číslo dne. Je to pořadové číslo konkrétního názvu pořadu.

Číslo se nikam neukládá. Při nové session se dopočítá z existujících EDL souborů na SD.
