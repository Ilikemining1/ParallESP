# ParallESP Parallel Port Emulator #

## Emulation of Parallel Printers using an ESP32 ##
The goals of the ParallESP project are as follows:
1. Full emulation of a Parallel Printer
2. The ability to either save .ps files on an SD card, or:
3. Forward the captured data on to another printer using the WiFi, including passing on Out-Of-Paper and Error signalling.
4. Easy configuration by "printing" a configuration document.

Currently, the project allows for the captured data to be printed out over serial, but this is subject to change as progress is made.

---

## Building ParallESP ##
In order to build ParallESP, you need to have the ESP-IDF installed, available from here: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html 

With the IDF installed, change into the directory you'd like to clone to, and run the following commands:
`git clone https://github.com/Ilikemining1/ParallESP.git
cd ParallESP`

Then, with the ESP32 plugged in, run:
`idf.py flash`

ParallESP is now installed on your ESP32.  To access the console:
`idf.py monitor`

---

## Wiring ##

Due to the fact that standard PC Parallel Ports use 5V logic levels, and the ESP32 is not 5V tolerant, level shifting is required for the following signals:

| ESP32 Pin	|	Function |
|-------|------------|
| GPIO4 |	/STROBE  |
| GPIO18|	D0       |
| GPIO19|	D1       |
| GPIO21|	D2       |
| GPIO22|	D3       |
| GPIO23|	D4       |
| GPIO25|	D5       |
| GPIO26|	D6       |
| GPIO27|	D7       |
| GPIO32|	/ACK     |
| GPIO33|	BUSY     |

In the future, more of the control signals, such as Error, will be implemented for control from the ESP.

---

## Project Status ##

Status of the project goals, in no particular order:

- [x] Initial Prototype using Serial output
- [ ] Add WiFi Support
- [ ] PCB Design
- [ ] SD Support
- [ ] "Print-based Configuration"

