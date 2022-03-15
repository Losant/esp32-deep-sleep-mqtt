# Battery Monitoring with Losant

For a more in-depth guide on how to use this code is in [this article]().

This project connects an ESP32 (using the [esp-idf](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)) to an MQTT broker (provided by [Losant](https://www.losant.com)) and collects the voltage of its battery source, publishes that voltage to an MQTT topic, and then sends the ESP32 into a deep sleep cycle for 15 minutes.

Since the ESP32 consumes a miniscule amount of power while in deep sleep, this project is great for IoT projects that make use of battery or solar power.

## To Use

Update the following with your own credentials in `main/app_main.c`:

```cpp
#define LOSANT_DEVICE_ID ""
#define LOSANT_ACCESS_KEY ""
#define LOSANT_ACCESS_SECRET ""

#define EXAMPLE_ESP_WIFI_SSID ""
#define EXAMPLE_ESP_WIFI_PASS ""
```

Run `idf.py build` and confirm successful build.

Upon an successful build, run `idf.py -p (PORT) flash monitor` which will flash the firmware to the designated port, and open the serial monitor.

To find which port your ESP32 is connected to, see the [esp-idf documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/establish-serial-connection.html) on establishing a serial connection with the ESP32.

## License

Copyright © 2022 Losant IoT, Inc. All rights reserved.

Licensed under the MIT license.

https://www.losant.com