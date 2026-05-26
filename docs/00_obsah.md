# Dokumentace projektu ATEM_LOGER_ESP-IDF

Tato složka obsahuje poznámky a popis projektu ESP-IDF / ESP32-P4 ATEM logger.

## Soubory

- `01_popis_projektu.md` – celkový popis projektu a aktuální stav
- `02_piny_a_hardware.md` – použité GPIO, sběrnice a hardware
- `03_komponenty.md` – přehled komponent projektu
- `04_edl_format.md` – formát EDL souborů pro DaVinci Resolve
- `05_rozdelni_jader.md` – rozdělení úloh mezi dvě jádra ESP32-P4
- `06_testovani.md` – doporučené testy po úpravách
- `07_uart_prikazy.md` – UART/debug příkazy
- `08_tichy_debug_rezim.md` – tichý výchozí UART/debug režim
- `09_web_rozhrani.md` – poznámky k webovému rozhraní
- `10_oled_start_ip.md` – startovací OLED obrazovka s ESP a ATEM IP adresou
- `11_nazvy_poradu_a_title.md` – uložené názvy pořadů a EDL TITLE
- `12_rtc_synchro.md` – synchronizace RTC z času prohlížeče
- `13_web_home_kosmetika.md` – Home místo Stav a barevně zvýrazněný aktivní pořad
- `14_web_nazvy_poradu_radio.md` – radio boxy vpravo za názvy pořadů, crimson puntík
- `15_web_aktivni_radek_crimson.md` – text aktivní v aktivním řádku crimson
- `16_auto_novy_soubor_po_zmene_poradu.md` – automatický nový EDL soubor po změně aktivního pořadu
- `17_web_about.md` – About stránka s popisem projektu a stručným výpisem funkcí
- `18_ochrana_souboru_proti_smazani.md` – checkbox ochrany EDL souboru proti smazání
- `19_web_checkbox_chraneno_align.md` – vystředění checkboxu Chráněno a menší mezera u Smazat
- `20_web_soubory_strihy.md` – sloupec Střihy v seznamu souborů a klikací název souboru
- `26_web_tabulka_auto_sirka.md` – CSS úprava tabulky souborů, aby se zbytečně neroztahovala

- `31_web_20_souboru_na_stranku.md` – výpis souborů po 20 záznamech na stránku

- `35_download_nazev_poradu.md` – stažení EDL pod názvem pořadu
- `36_web_filtr_souboru_se_strihy.md` – výchozí zobrazení jen souborů se střihy a tlačítko pro všechny soubory
- `37_web_filtr_pamatuje_rezim.md` – zapamatování režimu výpisu souborů do resetu a vždy viditelný aktuální soubor
- `38_web_filtr_prazdne_soubory.md` – režim výpisu jen pro soubory se Střihy = 0
- `39_web_vybrat_vse_zobrazene.md` – checkbox v hlavičce pro výběr všech zobrazených mazatelných souborů

- `41_web_about_astra.md` – věta na konci About stránky: Tento program napsala Astra, moje AI asistentka.

## Kam složku vložit

Složka `docs` patří do kořene projektu:

```text
ATEM_LOGER_ESP-IDF/
├── CMakeLists.txt
├── sdkconfig
├── main/
├── components/
└── docs/
```

ESP-IDF tyto soubory nekompiluje. Slouží pouze jako dokumentace k projektu.
- `21_web_velikost_vpravo.md` – zarovnání sloupce Velikost doprava
- `22_web_zobrazit_stahnout_stred.md` – zarovnání sloupců Zobrazit a Stáhnout na střed v tabulce souborů
- `25_web_sloupec_porad.md` – sloupec Pořad v seznamu souborů podle řádku TITLE

- `26_web_porad_auto_width_title_bez_cisla.md` – sloupec Pořad s automatickou šířkou a TITLE bez čísla

- `27_web_css_bob_style.md` – sjednocení web CSS podle Bobovy doladěné verze
- `28_web_stahnout_zelene_smazat_sede.md` – zelené odkazy Stáhnout a šedý text Smazat u chráněných souborů

- `29_web_aktualni_radek_a_smazani.md` – doladění aktuálního řádku a mazání chráněného aktuálního souboru

- `30_auto_ochrana_novych_souboru.md` – automatická ochrana nově vytvořených EDL souborů

- `32_web_preskrtnute_smazat.md` – přeškrtnuté neaktivní smazání u chráněných souborů

- `33_web_kopirovani_nazvu_poradu.md` – kopírování názvu pořadu ze seznamu souborů

- `34_web_kopirovani_bez_podtrzeni.md` – kopírování názvu pořadu bez tečkovaného podtržení

- `40_web_aktivni_filtr_barevne.md` – barevné odlišení aktivního režimu výpisu souborů

- `42_web_bez_diagnostiky.md` – odstranění diagnostické karty z Home stránky

- `43_web_home_tlacitka_v_karte.md` – tlačítka Home přesunutá do hlavní karty a nové pořadí

- `44_web_rtc_button_match.md` – sjednocení vzhledu tlačítka RTC Synchro na Home stránce

- `44_web_rtc_button_modre.md` – zvýrazněné tlačítko RTC Synchro na Home stránce

- `44_web_rtc_button_fix.md` – doladění tlačítka RTC Synchro bez ovlivnění ostatních tlačítek

- `44_web_rtc_font_match.md` – sjednocení fontu tlačítka RTC Synchro

- `45_web_formularova_tlacitka_svetle.md` – formulářová tlačítka se světlým písmem

- `42_rtc_synchro_priorita_css.md` – oprava priority CSS pro tlačítko RTC Synchro

- `46_web_live_home_state.md` – živý Home stav přes `/api/state`, běžící TC bez refresh stránky

- `47_web_novy_soubor_z_home.md` – tlačítko `Uzavřít a vytvořit nový` na Home, webová obdoba GPIO5

- `48_web_novy_soubor_ze_souboru.md` – tlačítko `Uzavřít aktuální a vytvořit nový` na stránce Soubory na SD kartě

- `49_web_active_show_ff8a8a.md` – aktivní pořad na Home barvou `#ff8a8a`
- `50_web_active_show_link.md` – aktivní pořad na Home jako odkaz na stránku Názvy pořadů
- `51_web_home_title_refresh.md` – klikací hlavička ATEM Logger na Home funguje jako refresh a spodní tlačítko Refresh je odstraněné

- [52 - Preview Tally vypínač](52_preview_tally_vypinac.md)
- [53 - Preview Tally okamžité uložení](53_preview_tally_okamzite.md)

- [54 - PVW Tally indikace na Home](54_web_home_pvw_tally.md)
- [Home: RTC datum a čas nahoře](55_web_rtc_nahore.md)

- [56 - Home: klikací datum/čas pro RTC synchronizaci](56_web_rtc_klikaci_sync.md)
- [57 - Build fix pro klikací RTC datum/čas](57_web_rtc_klikaci_sync_build_fix.md)
- [58 - Home: větší písmo a bez tlačítka RTC Synchro](58_web_home_vetsi_pismo_bez_rtc_tlacitka.md)

- `59_web_home_radky_pod_sebou.md` – Home stavové hodnoty rozdělené do samostatných řádků.

- `60_web_home_ltc_pred_tc.md` – LTC přesunuté před běžící timecode a timecode bez prefixu `TC:`.

- [61 Home: LTC a běžící TC v jednom řádku](61_web_home_ltc_tc_jeden_radek.md)
