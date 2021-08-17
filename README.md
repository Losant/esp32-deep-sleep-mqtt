# Battery Monitoring with Losant

Using the [ESP-IDF](https://github.com/espressif/esp-idf), connect an ESP32 an MQTT broker (provided by Losant in this example), publish battery voltage, then go to sleep for 15 minutes.

## To Use

Run `idf.py build` and confirm successful build.

Run `idf.py -p (PORT) flash monitor` which will flash the firmware to the designated port, and open the serial monitor. On Mac the command is `idf.py -p /dev/cu.SLAB_USBtoUART flash monitor`.
