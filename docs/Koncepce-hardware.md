# Koncepce hardwarové části

## 2.1 Přehled

Zařízení je rozděleno do menších celků s klíčovými komponentami:

*   **ESP32-S3:** Řídící jednotka.
*   **CH224Q/K:** Řízení Power Delivery.
*   **MCP3202:** 2x 12-bit A/D převodník.
*   **MCP4822:** 2x 12-bit D/A převodník.

Komunikace na desce probíhá přes I2C (PD čip, displej) a SPI (A/D a D/A převodníky).
Deska obsahuje filtrační kondenzátory, kontrolu teploty (NTC, ventilátor) a stavové RGB LED.

## 2.2 Napájecí kaskáda a Power Delivery

*   **ESD ochrana USB portu.**
*   **Napájení řídících prvků.**
*   **Nastavení USB PD:**
    *   Řadič CH224Q (s podporou PPS/AVS přes I2C).
    *   Řadič CH224K (bez PPS/AVS, nižší efektivita).
    *   Nouzový mód s 5,1kΩ rezistory (omezení na 5V 3A).
*   **DC/DC měnič AP63203:** Napájení klíčových komponent, potlačení EMI, PFM a FSS.

## 2.3 Řídící a komunikační jednotka

*   **SOC ESP32-S3 (FH4R2):** Dvoujádrový 32-bit LX7 CPU (240MHz), 4MB Flash, 2MB PSRAM, 2,4GHz WiFi, Bluetooth, I2C, SPI, UART, USB.
*   **Vestavěné A/D převodníky:** ADC1 pro kontrolu napětí na desce (referenční, nižší linearita). ADC2 multiplexován s WiFi.
*   **Konektivita:** SMD anténa s C-L-C článkem pro impedanční přizpůsobení.
*   **40MHz krystal.**
*   **USB datové linky:** Diferenciální pár s řízenou impedancí.

## 2.4 Výkonové a signálové výstupy

*   **Dva dvoukanálové 12-bit D/A konvertory MCP4822 (100kHz):** Jeden pro výkonovou, druhý pro signálovou část. Interní napěťová reference. Výstupy s 10nF kondenzátory pro potlačení harmonických.
*   **Zesílení signálu (LM324 - 4x OZ):**
    *   Analogové výstupy z D/A (B) na OZ (C, D) se zesílením 6,7x (16,5dB).
    *   Možnost úpravy zesílení (2x - 6,7x).
    *   Programovatelný výstupní zesilovač D/A (plný/poloviční rozsah).
    *   Rozlišení až 806µV.
*   **Výstupní konektor J3:** Nulový potenciál, napájení (3.3V, USB), GPIO, výkonové výstupy.
*   **Výkonové regulátory (řízené D/A A):** Komplementární dvojice MOSFETů.
    *   Napájecí část: CV režim.
    *   Zátěžová část: CC režim (referenčně vůči zemi).
    *   Digitální zpětná vazba pro přepínání CC/CV (může dojít k poklesu napětí).

## 2.5 Signálové vstupy

*   **Dvoukanálový 12-bit A/D převodník MCP3202 (100kHz).**
*   **Programovatelná odbočka z interní napěťové reference:** Úprava zesílení (plný/poloviční rozsah).
*   **Aktivní filtry (dolní propusti) 2. řádu Butterworth-Bessel:** Propustné pásmo 10kHz.
*   **Kalibrace zesílení vstupních OZ (U7A, U7B):** Připojením rezistoru na AREFx (musí být uzeměny, pokud se nepoužívají).
*   **Napěťový posuv vstupů:** Přes AREFx pro přesné měření v malém rozsahu.
*   **Vstupní napěťová rezerva:** Při 20V vstupního napětí není využit plný 12-bitový rozsah (horní hranice 3,3V).
*   **Napěťové rozlišení:** Cca 5,48 mV (lze zvýšit na 2,74 mV s polovičním rozsahem nebo úpravou R32/R33).
