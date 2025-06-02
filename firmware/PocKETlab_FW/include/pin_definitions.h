#ifndef PIN_DEFINITIONS_H
#define PIN_DEFINITIONS_H

// --- System Pins ---
// CHIP_PU/RESET is not a GPIO, but the reset pin.
// GPIO0 is often used for boot mode.
constexpr int PIN_BOOT_BUTTON = 0; // GPIO0, BOOT (Sheet 3, U3 Pin 5)

// --- Analog Feedback & Digital I/O (via AGPIO nets on Sheet 1, from ESP32 Sheet 3) ---
// These AGPIOx nets connect to various parts of the system.
// AGPIO0-4 seem to be feedback/control for DAC outputs or similar.
// AGPIO5-8 are general Digital I/O, possibly for ADC block or general use.
constexpr int PIN_FB_AO = 1;       // GPIO1 -> AGPIO0 -> FB_AO (Sheet 1) - FEEDBACK from DAC0
constexpr int PIN_FB_A1 = 2;       // GPIO2 -> AGPIO1 -> FB_A1 (Sheet 1) - FEEDBACK from DAC1
constexpr int PIN_FB_GOUT = 3;     // GPIO3 -> AGPIO2 -> FB_GOUT (Sheet 1) - FEEDBACK from floating ground (current limiter voltage)
constexpr int PIN_FB_IOUT = 4;     // GPIO4 -> AGPIO3 -> FB_IOUT (Sheet 1) - FEEDBACK from current limiter
constexpr int PIN_FB_VOUT = 5;     // GPIO5 -> AGPIO4 -> FB_VOUT (Sheet 1) - FEEDBACK from voltage output
constexpr int PIN_DIO0_ADC = 6;    // GPIO6 -> AGPIO5 -> DIO (Sheet 1, to ADC block) - Digital I/O 0
constexpr int PIN_DIO1_ADC = 7;    // GPIO7 -> AGPIO6 -> DI1 (Sheet 1, to ADC block) - Digital I/O 1
constexpr int PIN_DIO2_ADC = 8;    // GPIO8 -> AGPIO7 -> DI2 (Sheet 1, to ADC block) - Digital I/O 2
constexpr int PIN_DIO3_ADC = 9;    // GPIO9 -> AGPIO8 -> DI3 (Sheet 1, to ADC block) - Digital I/O 3

// --- Temperature Sensor ---
constexpr int PIN_TEMP_PROBE = 10; // GPIO10 -> AGPIO9 -> TEMP_PROBE (Sheet 1, J1 NTC)

// --- SPI Bus (for DACs U5, U6 and ADC U8) ---
// Connections traced from ESP32 (Sheet 3) to Sheet 1 nets, then to devices.
constexpr int PIN_DAC_LDAC = 11;    // GPIO11 -> GPIO11 net -> LDAC (Sheet 1, to U5, U6)
constexpr int PIN_SPI_MOSI = 12;    // GPIO12 -> GPIO12 net -> SPI_MOSI (Sheet 1, to U5, U6 SDI; U8 DIN)
constexpr int PIN_SPI_MISO = 13;    // GPIO13 -> GPIO13 net -> SPI_MISO (Sheet 1, from U8 DOUT)
constexpr int PIN_SPI_SCK = 14;     // GPIO14 -> GPIO14 net -> SPI_SCK (Sheet 1, to U5, U6 SCK; U8 CLK)

// --- Chip Selects for SPI Devices ---
// CS_SIG_ADC: ESP32 GPIO16 (Sheet 3, pin 22 XTAL_32K_N/GP1016) -> GP1016 net (Sheet 1) -> CS_SIG_ADC -> U8 CS (Sheet 5)
constexpr int PIN_CS_ADC_SIGNAL = 16;
// CS_PWR_DAC: ESP32 GPIO17 (Sheet 3, pin 23 GP1017) -> GP1017 net (Sheet 1) -> CS_PWR_DAC -> U6 CS (Sheet 4)
constexpr int PIN_CS_DAC_POWER = 17;
// CS_SIG_DAC: ESP32 GPIO18 (Sheet 3, pin 24 GP1018) -> GP1018 net (Sheet 1) -> CS_SIG_DAC -> U5 CS (Sheet 4)
constexpr int PIN_CS_DAC_SIGNAL = 18;

// --- Native USB (ESP32-S3 internal USB controller) ---
// These pins are typically handled by the USB stack.
constexpr int PIN_USB_D_MINUS = 19; // GPIO19 (Sheet 3, U3 Pin 25)
constexpr int PIN_USB_D_PLUS = 20;  // GPIO20 (Sheet 3, U3 Pin 26)

// --- PWM Outputs ---
constexpr int PIN_FAN_PWM = 21; // GPIO21 -> FAN net (Sheet 3) -> FAN_PWMD (Sheet 1, to Q1)

// --- RGB LED Data ---
// GPIO38 is used for LEDS_DATA (WS2812 type signal)
constexpr int PIN_RGB_LED_DATA = 38; // GPIO38 -> LEDS net (Sheet 3) -> LEDS_DATA (Sheet 1, to D1, D2, D3)

// --- Primary I2C Bus (for Power Delivery CH224K and LCD Connector J5) ---
// Note: ESP32-S3 uses GPIO47/48 for these, often associated with SPICLK_P/N if not I2C.
// Here, they are PRIM_SCL/SDA.
constexpr int PIN_I2C_SCL_PRIMARY = 47; // GPIO47 (SPICLK_P) -> SCL net (Sheet 3) -> PRIM_SCL (Sheet 1)
constexpr int PIN_I2C_SDA_PRIMARY = 48; // GPIO48 (SPICLK_N) -> SDA net (Sheet 3) -> PRIM_SDA (Sheet 1)

// --- UART0 (for Programming / Debug) ---
constexpr int PIN_UART_TXD0 = 43; // GPIO43 (U0TXD) (Sheet 3, U3 Pin 49)
constexpr int PIN_UART_RXD0 = 44; // GPIO44 (U0RXD) (Sheet 3, U3 Pin 50)


// --- ESP32 Input Pins (GPIO33-GPIO37) Monitoring Other Signals ---
// These GPIOs on the ESP32 (Sheet 3) are connected to nets (GPIO33-GPIO37)
// which on Sheet 1 are driven by outputs from the DAC and ADC blocks.
// This suggests they might be for monitoring/debugging those signals.

// GPIO33 (Sheet 3, U3 Pin 38) <- net GPIO33 <- DO0 from DAC block (Sheet 1)
// DO0 from DAC block (Sheet 4 hierarchical pin) is SCK of DACs.
constexpr int PIN_DIO0_DAC = 33;

// GPIO34 (Sheet 3, U3 Pin 39) <- net GPIO34 <- DO1 from DAC block (Sheet 1)
// DO1 from DAC block (Sheet 4 hierarchical pin) is LDAC of DACs.
constexpr int PIN_DIO1_DAC = 34;

// GPIO35 (Sheet 3, U3 Pin 40) <- net GPIO35 <- DO2 from DAC block (Sheet 1)
// DO2 from DAC block (Sheet 4 hierarchical pin) is SDI (MOSI) of DACs.
constexpr int PIN_DIO2_DAC = 35;

// GPIO36 (Sheet 3, U3 Pin 41) <- net GPIO36 <- DO3 from DAC block (Sheet 1)
// DO3 from DAC block (Sheet 4 hierarchical pin) is CS_POW (CS for U6 Power DAC).
constexpr int PIN_DIO3_DAC = 36;

// GPIO37 (Sheet 3, U3 Pin 42) <- net PRIM_SP <- SPL from Power block (Sheet 1)
constexpr int PIN_PD_SPL = 37;

#endif // PIN_DEFINITIONS_H
