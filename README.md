# ESP32-Blood-Pressure-Monitor
# Non-Invasive Blood Pressure Monitoring System Using ESP32

## Overview
This repository contains the embedded firmware for a non-invasive blood pressure (NIBP) monitoring prototype based on the oscillometric method. The system acquires real-time cuff pressure signals, filters arterial pulse oscillations, and computes Systolic Blood Pressure (SYS), Diastolic Blood Pressure (DIA), and Mean Arterial Pressure (MAP).

## Hardware Components
* **Microcontroller:** ESP32 DevKit V1
* **Pressure Sensor & ADC:** MPS20N0040D + HX710B (24-bit ADC module)
* **Display:** 0.96-inch I2C OLED Display (SSD1306, 128x64)
* **Peripherals:** Inflatable arm cuff with manual pump/release valve, Reset push button, Active buzzer

## Pinout Configuration
| Component | Pin | ESP32 Pin |
| :--- | :--- | :--- |
| **HX710B** | DOUT | GPIO 18 |
| **HX710B** | SCK | GPIO 19 |
| **OLED Display** | SDA | GPIO 21 |
| **OLED Display** | SCL | GPIO 22 |
| **Reset Button** | Signal | GPIO 4 |
| **Buzzer** | Signal | GPIO 23 |
