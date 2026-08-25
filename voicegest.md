---
publishDate: 2026-08-25
title: VoiceGest — A Gesture-to-Speech AAC Device for Non-Verbal Individuals
excerpt: A compact, portable communication device that turns simple deliberate movements — a hand tilt, a swipe, a breath, a colour card — into spoken phrases in real time, with no internet, no app subscription, and no surgical implant required.
image: https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/COVERPAGE_VOICEGEST.jpg
tags:
  - assistive-technology
  - aac
  - gesture-recognition
  - iot
  - esp32
  - myosa
  - biosensors
---

> A voice for the 70 million people worldwide who have been silenced by circumstance, not by choice.

---

## Acknowledgements

We express our sincere gratitude to our Faculty Mentor, **Neethu KC, Assistant Professor, Department of Electronics and Communication Engineering, Government Engineering College Thrissur**, for her continuous guidance, technical support, and encouragement throughout this project.

We thank the Department of Electronics and Communication Engineering and the management of **Government Engineering College Thrissur (GEC Thrissur), Kerala, India** for providing the resources and supportive environment that made this project possible.

We deeply appreciate the **IEEE International MYOSA Event 6.0 organizers** and the **IEEE BioSensors 2026 Track** for creating a platform that bridges academic learning with real-world engineering challenges aimed at social impact.

Finally, we thank each other — Team Cipher — for every sensor calibration, every gesture threshold tweak, and every late debugging session that turned this idea into a working device we believe can genuinely give people their voice back.

---

## Overview

Communication is a basic human right — yet an estimated 70 million non-verbal people worldwide, living with ALS, cerebral palsy, autism, and stroke-related aphasia, are denied it every day. Existing AAC (Augmentative and Alternative Communication) devices cost upwards of ₹6,00,000 and demand fine motor precision that many users no longer have, placing them entirely out of reach for the vast majority of patients in low- and middle-income countries.

**VoiceGest** is a compact, portable communication device built on the MYOSA Mini IoT Kit (ESP32-based) that converts simple, deliberate movements — a hand tilt, a swipe, a breath, a colour card — into spoken phrases in real time. It needs no internet, no app subscription, and no surgical implant — just whatever voluntary movement a user has left.

VoiceGest reads gesture, motion, and breath through the MYOSA MPU6050, APDS9960, and BMP180 sensor boards, fuses the data on-device on the ESP32, and speaks the result aloud through a paired phone via Bluetooth. Every phrase is confirmed on an OLED display before being spoken, giving the user a chance to cancel a misread gesture, and every communication event can optionally be logged with a timestamp to a self-hosted caregiver dashboard.

**Key Features:**

- Multi-modal gesture-to-speech — tilt, rotation, swipe, colour card, and breath all map to spoken phrases
- 20-phrase vocabulary library stored entirely on-device in ESP32 flash memory
- Proximity-gated intent detection — filters out accidental tremors, only classifies deliberate gestures
- On-screen phrase confirmation via OLED before speaking, with a 1-second hold-to-confirm / move-to-cancel gesture
- Bluetooth speech output — works with any free Bluetooth terminal / text-to-speech app, no custom app required
- Optional Wi-Fi caregiver dashboard with live, timestamped, exportable communication log
- Fully offline operation — zero internet, zero cloud dependency, zero recurring cost
- Roughly a fraction of the cost of commercial AAC devices, built entirely on the MYOSA sensor stack

---

## Demo / Examples

### Images

![VoiceGest — fully assembled device with all three sensor boards connected](https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/COVERPAGE_VOICEGEST.jpg)
*VoiceGest — fully assembled device with all three sensor boards connected*

![MYOSA Mini Kit components and sensor boards used in the build](https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/kit_components.jpg)
*MYOSA Mini Kit components and sensor boards used in the build*

![OLED display showing 'Status: Ready' at power-on](https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/OLED_Ready_event1.jpg)
*OLED display showing 'Status: Ready' at power-on*

![OLED display confirming the classified phrase "I am in pain" before it is spoken](https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/OLED_output_pain.jpg)
*OLED display confirming the classified phrase "I am in pain" before it is spoken*

![OLED display confirming the classified phrase "I need water" before it is spoken](https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/OLED_output_water.jpg)
*OLED display confirming the classified phrase "I need water" before it is spoken*

![OLED display confirming the classified phrase "NO" during YES/NO dialogue](https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/OLED_output_no.jpg)
*OLED display confirming the classified phrase "NO" during YES/NO dialogue*

![Serial monitor confirming successful initialization of all sensors](https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/serial-monitor-ready.jpg)
*Serial monitor confirming successful initialization of all sensors*

![Caregiver dashboard showing the live, timestamped communication event log, viewed on a phone](https://raw.githubusercontent.com/gayathrium4567/VOICEGEST/main/Dashboard-phone.jpg)
*Caregiver dashboard showing the live, timestamped communication event log, viewed on a phone*

### Videos

Plain markdown can't embed a playable video player, so these link straight to the file — tap to open/download and play:

- [▶ Watch: VoiceGest live demonstration](https://github.com/gayathrium4567/VOICEGEST/blob/main/voicegest_demo_video.mp4) — baseline, basic need, YES/NO dialogue, emergency alert, colour card, and breath input
- [▶ Watch: Caregiver dashboard walkthrough](https://github.com/gayathrium4567/VOICEGEST/blob/main/Dashboard_video.mp4)

---

## Features (Detailed)

### 1. Tilt-Based Basic Needs (MPU6050 — Accelerometer)

The MPU6050 accelerometer continuously reads tilt direction along the X, Y, and Z axes of the enclosure. Four tilt directions are mapped to core needs:

- Forward → "I need help"
- Backward → "I am hungry"
- Left → "I am in pain"
- Right → "I need water"

This lets a user resting the device on a lap or wheelchair tray communicate a basic need with a single deliberate motion.

### 2. Rotation and Double-Tap Dialogue (MPU6050 — Gyroscope)

The gyroscope on the same MPU6050 board detects rotation and double-tap patterns, enabling quick YES/NO-style dialogue:

- Clockwise rotation → "NO"
- Anti-clockwise rotation → "Thank you"
- Double-tap pattern → "EMERGENCY" (triggers continuous buzzer and a loud spoken alert)

### 3. Hand Swipe Gestures (APDS9960 — Gesture Sensor)

The APDS9960 gesture engine detects directional hand swipes above the device:

- UP → "Call my doctor"
- DOWN → "I want to go home"
- LEFT → "I feel good"
- RIGHT → "I need medicine"

### 4. Proximity-Gated Intent Detection (APDS9960 — Proximity Sensor)

The same APDS9960 board doubles as an intent gate: a gesture is only classified when a hand is within a deliberate interaction range (<10 cm). This filters out accidental tremors or involuntary movements — a critical safeguard for users with limited motor control — and means a single sensor performs both gesture recognition and false-trigger rejection.

### 5. Colour Card Vocabulary (APDS9960 — RGB Sensor)

The same sensor's RGB channel identifies colour cards held near the enclosure, giving users who cannot move the device itself a third, independent input channel:

- RED → "Emergency"
- GREEN → "I feel okay"
- BLUE → "I want to rest"
- YELLOW → "I am happy"

### 6. Breath-Triggered Politeness Prefix (BMP180 — Barometric Pressure)

The BMP180 detects an intentional controlled breath near the enclosure's vent as a pressure spike (delta > 50 Pa). When detected, it prepends "PLEASE" to whatever phrase follows — e.g. "PLEASE, I need help" — giving even users with only breath control an independent communication channel.

### 7. On-Screen Confirmation (SSD1306 OLED)

Every classified phrase is shown on the OLED before it is spoken. The user confirms by holding still for one second, or cancels by moving — preventing misread gestures from being spoken aloud.

### 8. Audio Feedback (Buzzer)

A single beep confirms a gesture was registered and spoken; a double beep signals a misread gesture that needs to be retried. This gives non-visual confirmation for users with visual impairment.

### 9. Bluetooth Speech Output

The confirmed phrase is sent as text via Bluetooth Serial to a paired phone. No custom app is required — any free Bluetooth terminal app configured to read incoming text aloud (using the phone's built-in Text-to-Speech engine) will speak the phrase.

### 10. Optional Wi-Fi Caregiver Dashboard

The ESP32 can simultaneously host a lightweight self-contained web dashboard over Wi-Fi, showing a live, timestamped communication log accessible from any phone or laptop browser and exportable to CSV — while Bluetooth handles real-time speech output. Both wireless channels run concurrently from the same board.

---

## Experimental Results

VoiceGest was tested through a series of simulated non-verbal communication scenarios, including:

- Tilt-based basic-need signalling in all four directions
- Rotation-based YES/NO dialogue and double-tap emergency triggering
- Directional hand swipe recognition at varying distances
- Colour card recognition under different lighting conditions
- Breath-detection sensitivity and false-positive filtering
- Extended continuous operation on battery power

**Results:**

- Gesture classification completed reliably within 200 ms of a deliberate movement
- Proximity gating successfully filtered out incidental hand movement and tremor-like motion
- OLED confirm/cancel step prevented misread gestures from being spoken
- Stable Bluetooth Serial connection to phone with no noticeable speech delay
- 8+ hours of continuous operation on a single 1000 mAh LiPo charge
- (If enabled) stable concurrent Wi-Fi dashboard operation alongside Bluetooth speech output

---

## Usage Instructions

```
Step 1: Connect JST sensor chain
Motherboard → OLED → MPU6050 → APDS9960 → BMP180

Step 2: Power on device
Use the onboard 3.7V LiPo battery (charged via the TP4056 module) or a 5V USB power bank

Step 3: Pair with phone
Open a Bluetooth terminal app on your phone, pair with the device, and enable
"read incoming text aloud" / text-to-speech mode

Step 4: Interact
Tilt, rotate, swipe, show a colour card, or blow gently near the vent —
whichever gesture matches your current voluntary movement ability

Step 5: Confirm or cancel
Hold still for 1 second to confirm and speak the phrase, or move to cancel
a misread gesture

Step 6 (optional): Caregiver dashboard
Connect to the device's Wi-Fi hotspot and open the dashboard in a browser
to review the live, timestamped communication log
```

---

## Tech Stack

- **MYOSA Mini IoT Kit** — ESP32-WROOM-32E motherboard
- **MPU6050** — accelerometer + gyroscope sensor board (tilt and rotation gestures)
- **APDS9960** — gesture, proximity, and RGB colour sensor board (swipe, intent gate, colour cards)
- **BMP180** — barometric pressure sensor board (breath detection)
- **SSD1306 OLED** — phrase display and confirmation UI
- **Arduino C++** — on-device firmware, gesture classification, and Bluetooth/Wi-Fi handling
- **Bluetooth Serial** — real-time speech output to a paired phone
- **Wi-Fi (softAP, optional)** — self-hosted caregiver dashboard
- **3.7V LiPo battery + TP4056** — portable power for 8+ hours of use

---

## Requirements / Installation

```bash
# Arduino IDE board support
# Install "ESP32" boards via Boards Manager (espressif/arduino-esp32)

# Required Arduino libraries
Wire.h              # I2C communication
Adafruit_MPU6050    # accelerometer + gyroscope
Adafruit_APDS9960   # gesture, proximity, RGB
Adafruit_BMP085     # barometric pressure (BMP180-compatible)
Adafruit_SSD1306    # OLED display
BluetoothSerial     # Bluetooth Serial output (built into ESP32 core)
```

```
1. Clone this repository: https://github.com/gayathrium4567/VOICEGEST
2. Open voicegest.ino in Arduino IDE
   (source: https://github.com/gayathrium4567/VOICEGEST/blob/main/voicegest.ino)
3. Select your ESP32 board under Tools > Board
4. Install the libraries listed above via Library Manager
5. Upload to the MYOSA motherboard
6. Pair a phone via Bluetooth and enable text-to-speech on incoming messages
```

---

## File Structure

```
/voicegest
  ├── voicegest.md
  ├── COVERPAGE_VOICEGEST.jpg
  ├── kit_components.jpg
  ├── OLED_Ready_event1.jpg
  ├── OLED_output_no.jpg
  ├── OLED_output_pain.jpg
  ├── OLED_output_water.jpg
  ├── serial-monitor-ready.jpg
  ├── Dashboard-phone.jpg
  ├── voicegest_demo_video.mp4
  ├── Dashboard_video.mp4
  └── voicegest.ino
```

---

## License

MIT License — free to use, modify, and distribute with attribution.

Developed by Team Cipher for IEEE International MYOSA Event 6.0, IEEE BioSensors 2026 Track.

---

## Contribution Notes

This project was developed as a functional prototype for the IEEE International MYOSA Event 6.0, IEEE BioSensors 2026 Track.

Contributions, real-world deployment adaptations, and vocabulary-expansion ideas (e.g. multilingual phrase libraries, caregiver-customisable vocabularies via microSD) are welcome via issues or pull requests.

**Team Cipher**
Gayathri U M · Abbhiramie Ajayan
Government Engineering College Thrissur
Kerala, India
