# Reflow-Controller

**This page is a work in progress!**

The Reflow-Controller was built to control different kinds of reflow ovens, hotplates and whatever is coming to my mind in the future. It is based on the ESP32-S2 [datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s2_datasheet_en.pdf) and thus can be programmed to be used with a WiFi App or whatever. Nevertheless, it has an onboard OLED display and three buttons included.

The PCB occupies these features:

- ESP32-S2 MCU
- 2x MAX6675 Thermocouples
- 2x Fan output (inductive loads/flyback diodes included)
- 2x Solid state relay output
- 3x User button
- 1x APA102 user LED
- 1x Buzzer
- 1x Servo motor output
- 0,96" OLED display
- Optional ACDC converter (HLK-PM05, HLK-PM12) which can be break off if it isn't used
- Optional I2C (QWIIC) connector


## GPIOs

The ESP32-S2 is connected like this:

Function | GPIO | Mode
-------- | -------- | --------
SSR1 | GPIO1 | Output
SSR2 | GPIO2 | Output
Servo | GPIO3 | Output
Fan1 | GPIO5 | Output
Fan2 | GPIO6 | Output
CLK | GPIO7 | SPI
MISO | GPIO9 | SPI
MOSI | GPIO11 | SPI
CS1 | GPIO12 | SPI
CS2 | GPIO13 | SPI
Buzzer | GPIO15 | Output
USB D- | GPIO19 | CDC
USB D+ | GPIO20 | CDC
SDA | GPIO33 | I2C
SCL | GPIO35 | I2C
Button1 | GPIO36 | Input
Button2 | GPIO39 | Input
Button3 | GPIO40 | Input
LED CLK | GPIO37 | LED
LED DATA | GPIO38 | LED
OLED RST | GPIO45 | Output

## ACDC power supply

The top part of the PCB can be used as a power supply for the whole board as well as the higher voltage outputs (SSRs, Fans). It comes with an LM1117 3.3v regulator which can be feed with max 15v. The output voltage of the ACDC power supply is the voltage for the SSR and the fan output channels.
The HLK-PMXX module for the top part of the PCB can be placed in two ways - on the bottom side and on the top side. This gives the possibility to mount the whole PCB behind an acrylic glass (or whatever) so that the buttons as well as the OLED display are accessible. Therefore the two jumpers can select which signal is VCC and which is GND. **Keep in mind that there are high voltage pins on the top side of the PCB when mounting the power supply on the bottom side! I've designed a 3D-printable cover for the top part, so that these pins cannot be touched accidentally. (Should be mounted in both cases)**

The board can also be powered via USB-C, but keep in mind that the output channels are not powered in this case (initially it is just available for flashing the board).


## Reflow Profiles
