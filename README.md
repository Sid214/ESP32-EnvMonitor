# ESP32 Professional Environment Monitor v3.0 🌡️💧

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-lightgrey)
![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-00979C)

A professional, real-time IoT environmental monitoring station powered by the ESP32. This project features a highly responsive, modern web dashboard (with a built-in dark/light theme toggle) alongside a fully independent offline OLED display for physical monitoring.

## 📸 Dashboard Preview

### Live Dashboard
![Home Dashboard](Home%20UI.png)
*The main web interface displaying live, smoothed sensor data, trends, and calculated metrics.*

### Custom Environment Profiles
![Custom Settings](Custom%20settings.png)
*The settings panel allowing users to set threshold alarms and manage environment profiles.*

---

## ✨ Key Features

* **Dual-Interface Architecture:** View live data via the responsive web dashboard or directly on the physical SSD1306 OLED screen (displaying both °C and °F).
* **Smart Data Smoothing:** Implements an Exponential Moving Average (EMA) filter to stabilize DHT11 sensor readings, ensuring high accuracy and preventing UI flickering.
* **Offline Failsafe Mode:** If the designated Wi-Fi network drops or is unavailable, the web server is bypassed. The ESP32 continues to function autonomously as a local monitor, displaying hardware-level alerts on the OLED.
* **Non-Volatile Memory (EEPROM):** Custom alert thresholds and selected environment profiles are saved to the ESP32's flash memory via `Preferences.h` and survive power cycles.
* **Browser Audio Siren:** Integrated Web Audio API triggers a high-visibility alert banner and an audible siren on connected devices when environmental thresholds are breached.
* **Advanced Metrics & Analytics:** Automatically calculates and graphs Heat Index, Dew Point, and Session Min/Max historical trends.

## 🛠️ Hardware Requirements

* **Microcontroller:** ESP32 Development Board (e.g., DOIT DevKit V1)
* **Sensor:** DHT11 Temperature & Humidity Sensor
* **Display:** 0.96" SSD1306 OLED Display (I2C)
* **Power:** Standard 5V Micro-USB or USB-C power supply

## 🔌 Wiring Guide

| Component | Pin | ESP32 Connection | Notes |
| :--- | :--- | :--- | :--- |
| **DHT11** | VCC | `3V3` | Use 3.3V power rail |
| | GND | `GND` | Common ground |
| | DATA | `GPIO 4` | Can be remapped in `config.h` |
| **OLED** | VCC | `3V3` | Use 3.3V power rail |
| | GND | `GND` | Common ground |
| | SDA | `GPIO 21` | Hardware I2C |
| | SCL | `GPIO 22` | Hardware I2C |

## 💻 Software Installation

1. Install the **Arduino IDE** and configure it for the ESP32 board package via the Boards Manager.
2. Install the following dependencies via the Arduino Library Manager:
   * `Adafruit SSD1306` by Adafruit
   * `Adafruit GFX Library` by Adafruit
   * `DHT sensor library` by Adafruit (Ensure `Adafruit Unified Sensor` is also installed)
3. Clone this repository or download the source code as a ZIP file.
4. Open `temperature_and_humidity.ino` and configure your network credentials:
   ```cpp
   const char* ssid     = "YOUR_WIFI_NETWORK";
   const char* password = "YOUR_WIFI_PASSWORD";
