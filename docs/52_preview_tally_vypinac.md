# Preview Tally vypínač

Na webové stránce **Nastavení** je nad IP adresami položka **Preview Tally**.

- zaškrtnuto = zelené Preview tally výstupy jsou aktivní
- odškrtnuto = Preview/PVW tally výstupy jsou vypnuté a drží se zhasnuté
- Program/PGM tally výstupy zůstávají funkční vždy

Stav se ukládá do NVS ESP32-P4, tedy přetrvá restart stejně jako IP adresy.

Vypnutí Preview tally se projeví v komponentě `tally_outputs`; web pouze uloží nastavení a vlastní výstupy se dál obnovují v běžné UI taskce.
