# 53 - Preview Tally okamžité uložení

Na stránce `/network` je checkbox `Preview Tally` oddělený od ukládání IP adres.

Chování:

- zaškrtnuto = zelené Preview tally výstupy jsou aktivní,
- odškrtnuto = Preview tally výstupy jsou vypnuté a drží se zhasnuté,
- změna checkboxu se uloží hned do NVS přes `/save_preview_tally`,
- není potřeba potvrzovat tlačítkem `Uložit`,
- IP adresy se ukládají samostatně tlačítkem `Uložit nastavení IP`.

Stránka se po změně checkboxu vrátí zpět na `/network`.
