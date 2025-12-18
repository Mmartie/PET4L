# PET4L - Productivity Emotional Tool for Learning
> **Note:** This repository contains the source code and technical documentation associated with the **Final Laboratory Report** for the PET4L project.

## Project Description
PET4L is an interactive robotic flower designed to assist users with time management and productivity using the **Pomodoro Technique**. The system employs a "Biofeedback" approach: the flower blooms and thrives when the user is present and productive, but "withers" (*Withering Mode*) if the user abandons the workstation during a work cycle.
The software is built upon a non-blocking **Finite State Machine (FSM)** architecture that simultaneously manages sensors, actuators, and LED feedback without interruption.

## Hardware Requirements
To replicate this project, the following components are required:
* **Microcontroller:** Arduino Uno / Nano (or compatible)
* **Actuator:** Micro Servo Motor (e.g., SG90)
* **Sensor:** Ultrasonic Module HC-SR04
* **Lighting:** NeoPixel LED Ring (20 RGB LEDs, e.g., WS2812B)
* 3D Printed Case and Petals (refer to the attached STL files or the report)
* Jumper wires and power supply

## Wiring Connection Guide
| Component | Component Pin | Arduino Pin | Notes |
| :--- | :--- | :--- | :--- |
| **Ultrasonic Sensor (HC-SR04)** 
| | VCC | 5V | Common Power Rail |
| | GND | GND | Common Ground Rail |
| | Trig | **D9** | Digital Pin |
| | Echo | **D10** | Digital Pin |
| **Micro Servo** |
| | VCC (Red) | 5V | Common Power Rail |
| | GND (Brown/Black)| GND | Common Ground Rail |
| | Signal (Orange)| **D3** | PWM Pin |
| **NeoPixel Ring** 
| | VCC / 5V | 5V | Common Power Rail |
| | GND | GND | Common Ground Rail |
| | Data In (DI) | **D6** | Digital Pin |

## Required Libraries
The code relies on the following libraries, which must be installed via the Arduino Library Manager or imported manually:
1.  **`VarSpeedServo.h`**: Used for controlling servo speed to create organic, fluid movements.
    * *Note:* If not found in the standard Library Manager, download the .zip from GitHub and install it manually.
2.  **`Adafruit_NeoPixel.h`**: Used for controlling the LED Ring animations.

## Key Features
The code implements the following logic as detailed in the report:
* **Pomodoro Cycle:** 25 minutes of Work / 5 minutes of Break.
* **Presence Detection:** Algorithm with median filtering and hysteresis to reject false positives/negatives from the ultrasonic sensor.
* **Anti-Jittering:** Automatic servo de-powering (`detach`) when stationary to save energy and eliminate noise.
* **Withering Animation:** If the user leaves the workspace for more than 5 seconds during a work cycle, the flower performs an "Angry Dance" and subsequently withers (Dramatic Closing).
* **Gamification:** A Level System with **Color Coding** (Yellow -> Purple) that progresses with every completed cycle.
* **Software Interrupt:** Immediate responsiveness if the user returns during blocking animations (e.g., while withering).

## Authors
Code developed by group 3 (Didrik Bydal, Martina Mesiano, Yu Lu, Rubankarthik  Umapathy) for the Mechathronics for Product Design course in KTH.
Please refer to the full report for details on the design process.
