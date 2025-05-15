# Specifikace cílových požadavků na zařízení

Z koncepce vyplívá rozdělení zařízení na 3 části:

*   **Řídící část:** Zajišťující komunikaci a ovládání všech nastavení zařízení, včetně nastavení provozních hodnot a ochran proti přetížení.
*   **Měřící (analyzační část):** Sestávající z A/D a D/A převodníků a jim přidělených zesilovačů a filtrů.
*   **Napájecí (spínací) část:** Schopná měřené prvky napájet či zatěžovat.

Takto navržené zařízení by mělo být vhodné pro většinu základních měření, jako jsou:

*   V-A charakteristiky diod
*   Teplotní závislosti NTC a PTC prvků (při přidání topného tělesa)
*   Měření zesílení tranzistorů
*   Frekvenční charakteristiky aktivních i pasivních nf filtrů
*   Zatěžovací charakteristiky usměrňovačů
*   Průběhy (pomalejších) přechodových dějů

## 1.1 Specifikace napájecího systému

Napájecí kaskáda zařízení musí být schopna dosáhnout provozních napětí a proudů základních el. prvků, pro jejichž měření je zařízení určeno. Cílové parametry:

*   Napětí: do 12 V (max. 24 V)
*   Proud: do 1-2 A
*   Celkový ztrátový výkon: do 60 W (maximum pro USB-C PD bez EPR a E-Mark)

Výběr USB-C jako napájecího standardu umožňuje využití široce dostupných zdrojů. Regulace výstupního napětí PD zdroje je možná krokově (5, 9, 12, 15, 20 V) nebo pomocí PPS/AVS pro jemné krokování. Zařízení musí být vybaveno vlastním regulovatelným zdrojem (0-20 V) s možností lineární i spínané regulace a proudové regulace.

Druhou částí je spínaná zátěž pro testování zatěžovacích charakteristik s regulací ztrátového výkonu (proudově či napěťově).

## 1.2 Specifikace měřícího systému

Měřící systém budí měřený obvod a měří jeho odezvu.

*   Možnost zesílení výstupu nebo spojení s regulovatelnou napájecí kaskádou.
*   Stejné nebo vyšší rozlišení vstupu než výstupu.
*   Dva signálové vstupy a výstupy (pro pseudo-diferenciální mód nebo testování stereo zařízení).

## 1.3 Specifikace řízení

*   Řízení moderním mikropočítačem s dostatečnou pamětí a rychlostí.
*   Vestavěný A/D převodník pro kontrolu napětí v různých částech zařízení.
*   USB řadič pro přímé propojení s PC.
*   WiFi/Bluetooth konektivita pro vzdálené připojení.

## 1.4 Specifikace řídící aplikace

*   Přehledná a jednoduchá aplikace.
*   Zobrazování měřených dat (i v reálném čase).
*   Ukládání charakteristik.
*   Nastavování parametrů měření.
*   Optimalizace pro mobilní zařízení (tablety, chytré telefony).
