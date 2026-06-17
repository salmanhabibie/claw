#pragma once

/* ============================================================================
 * Pin map for the Waveshare ESP32-S3-Touch-LCD-1.46B
 * (taken from the board's "Internal Hardware Connections" table)
 * ============================================================================ */

/* ---- Shared I2C bus (touch SPD2010 @0x53, TCA9554 expander, IMU, RTC) ---- */
#define BSP_I2C_SCL        10
#define BSP_I2C_SDA        11

/* ---- LCD: SPD2010, QSPI interface, 412 x 412 ---- */
#define BSP_LCD_SCK        40
#define BSP_LCD_CS         21
#define BSP_LCD_D0         46   /* LCD_SDA0 */
#define BSP_LCD_D1         45   /* LCD_SDA1 */
#define BSP_LCD_D2         42   /* LCD_SDA2 */
#define BSP_LCD_D3         41   /* LCD_SDA3 */
#define BSP_LCD_TE         18   /* tearing effect (unused for now) */
#define BSP_LCD_BL         5    /* backlight, direct GPIO */

/* ---- Touch: SPD2010 (I2C). Reset is on the expander (see below). ---- */
#define BSP_TP_INT         4

/* ---- TCA9554 GPIO expander ----
 * The LCD reset, touch reset and SD chip-select are NOT direct GPIOs; they
 * hang off the TCA9554 I2C expander. The board labels them EXIO1..EXIO3.
 *
 * NOTE: the EXIOn -> expander-pin mapping is the single most likely thing to
 * need adjusting if the screen stays black. We assume EXIOn == expander Pn.
 * If nothing shows up, try shifting these by one (EXIO1 -> PIN_NUM_0, etc.). */
#define BSP_EXIO_TP_RST    IO_EXPANDER_PIN_NUM_1   /* EXIO1 */
#define BSP_EXIO_LCD_RST   IO_EXPANDER_PIN_NUM_2   /* EXIO2 */
#define BSP_EXIO_SD_CS     IO_EXPANDER_PIN_NUM_3   /* EXIO3 */

/* ---- Speaker: PCM5101 DAC (I2S out) — for the later voice phase ---- */
#define BSP_SPK_DIN        47
#define BSP_SPK_LRCK       38
#define BSP_SPK_BCK        39

/* ---- Microphone (I2S in) — for the later voice phase ---- */
#define BSP_MIC_WS         2
#define BSP_MIC_SCK        15
#define BSP_MIC_SD         6

/* ---- microSD card (SPI). CS is on the expander (EXIO3). ---- */
#define BSP_SD_MISO        16
#define BSP_SD_MOSI        17
#define BSP_SD_SCK         14

/* ---- Display geometry ---- */
#define BSP_LCD_H_RES      412
#define BSP_LCD_V_RES      412
