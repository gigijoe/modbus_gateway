#
# GPIO6-11 are usually used for SPI flash.
# GPIO20, GPIO24, and GPIO28-31 are not available as pins.
# GPIO34-39 can only be set as input mode and do not have software pullup or pulldown functions.
#

https://nodemcu.readthedocs.io/en/dev-esp32/en/modules/gpio/

#
#
#

0.Boot strap
1.UART - U0TXD 
2.HSPI - MISO / SDIO - DATA0
3.UART - U0RXD  
4.SDIO - DATA1
5.GPIO - DS18B20 
6. FLASH / PSRAM - SPICLK_OUT_IDX
7. FLASH / PSRAM - SPIQ_OUT_IDX / SPID_IN_IDX
8. FLASH / PSRAM - SPID_OUT_IDX / SPID_IN_IDX
9. FLASH / PSRAM - SPIHD_OUT_IDX / SPIHD_IN_IDX
10.FLASH / PSRAM - SPIWP_OUT_IDX / SPIWP_IN_IDX
11.FLASH
12.GPIO - Button
13.SDIO - DATA3 / CS
14.HSPI - SCK / SDIO - SCK
15.HSPI - MOSI / SDIO - CMD
16.PSRAM_CS_IO - SPICS1_OUT_IDX
17.PSRAM_CLK_IO - SIG_IN_FUNC225_IDX
18.UART - U1TXD 
19.UART - U1RXD  
20.X
21.I2C SDA
22.W5500 - CS
23.GPIO - Buzzer
24.X
25.LED Green
26.W5500 - Reset
27.LED Blue
28.X
29.X
30.X
31.X
32.I2C SCL
33.JUMPER
34.Input only - PCF8574 Interrupt 1
35.Input only - PCF8574 Interrupt 2
36.Input only - ADC
37.X
38.X
39.Input only - W5500 - Interrupt

