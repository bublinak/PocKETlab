# Uživatelská aplikace

Software pro PocKETlab se skládá ze dvou částí:

1.  **Uživatelské rozhraní:** Pro správu zařízení.
2.  **Firmware:** Běžící na desce ESP32-S3, stará se o řízení periferií.

Komunikace mezi nimi je realizována pomocí protokolu **MQTT**.

## Webová aplikace (Flask)

Pro správu zařízení běží na serveru jednoduchá aplikace vytvořená pomocí nástroje **Flask** (Python). Umožňuje:

*   Nastavení parametrů měření.
*   Spuštění měření.
*   Export dat.
*   Vizualizaci dat.
*   Jednoduchou správu uživatelů (SQLite databáze, přihlášení/registrace).

### Typy měření:

Aplikace umožňuje provádět 4 typy měření:

1.  **V-A charakteristiky:** V režimu CC (Constant Current) nebo CV (Constant Voltage).
2.  **Frekvenční a fázová odezva:** (Bodeho diagram).
3.  **Skoková odezva.**
4.  **Impulzní odezva.**

### Omezení parametrů:

Ovládací prvky jsou ošetřeny proti zadání neplatných parametrů:

*   **Výstupní napětí:** 0-20V
*   **Výstupní proud:** 0-3A
*   **Frekvenční rozsah:** 10Hz-10kHz
*   **Délka impulzu:** min. 10µs (dáno rychlostí D/A převodníku a komunikace)

### Vizualizace:

Pro vizualizaci grafů je využita knihovna **Plotly**. Grafy jsou interaktivní a lze je exportovat do formátu PNG.

### Budoucí vývoj:

Aplikace je ve fázi MVP (Minimum Viable Product). Plánované vylepšení:

*   Vyznačení časové konstanty u skokové odezvy.
*   Oprava škálování grafů.
*   Správa týmů a registrovaných zařízení (pro nasazení ve výuce).
