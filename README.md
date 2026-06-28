# ESP32 Environment Monitor v3.0 🌡️💧

A professional, real-time IoT environmental monitoring station powered by the ESP32. This project features a highly responsive local web dashboard with a built-in dark/light theme, alongside a fully independent offline OLED display for physical monitoring.

## ✨ Key Features

* **Dual-Interface Monitoring:** View live data via the web browser dashboard or directly on the physical SSD1306 OLED screen (displaying both °C and °F).
* **Smart Data Smoothing:** Implements an Exponential Moving Average (EMA) filter to stabilize DHT11 sensor readings and prevent UI flickering.
* **Offline Failsafe Mode:** If the designated Wi-Fi network is unavailable, the web server is bypassed, and the ESP32 continues to function as a standalone local monitor, displaying alerts on the OLED.
* **Environment Profiles:** One-click presets for specific environments (Server Room, Greenhouse, Office, Wine Cellar, Bedroom) alongside a fully customizable user profile.
* **Non-Volatile Memory:** Custom alert thresholds and selected profiles are saved to the ESP32's flash memory via `Preferences.h` and survive power reboots.
* **Browser Audio Siren:** Integrated Web Audio API triggers a high-visibility alert banner and an audio siren when thresholds are breached.
* **Advanced Metrics:** Automatically calculates and graphs Heat Index, Dew Point, and Session Min/Max trends.

## 🛠️ Hardware Requirements

* **Microcontroller:** ESP32 Development Board (e.g., DOIT DevKit V1)
* **Sensor:** DHT11 Temperature & Humidity Sensor
* **Display:** 0.96" SSD1306 OLED Display (I2C)
* **Misc:** Breadboard, Jumper wires, 5V USB power supply

## 🔌 Wiring Guide

| Component | Pin  | ESP32 Connection |
| :--- | :--- | :--- |
| **DHT11** | VCC | `3V3` |
| | GND | `GND` |
| | DATA | `GPIO 4` |
| **OLED** | VCC | `3V3` |
| | GND | `GND` |
| | SDA | `GPIO 21` |
| | SCL | `GPIO 22` |

## 💻 Software Setup

1. Install the **Arduino IDE** and configure it for the ESP32 board package.
2. Install the following required libraries via the Arduino Library Manager:
   * `Adafruit SSD1306` by Adafruit
   * `Adafruit GFX Library` by Adafruit
   * `DHT sensor library` by Adafruit (and the associated `Adafruit Unified Sensor` dependency)
3. Open the `.ino` file and update the Wi-Fi credentials:
   ```cpp
   const char* ssid     = "YOUR_WIFI_NETWORK";
   const char* password = "YOUR_WIFI_PASSWORD";
