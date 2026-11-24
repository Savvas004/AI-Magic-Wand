# AI Magic Wand 🪄

**An ESP32-S3 gesture-controlled wireless mouse for macOS, powered by on-device machine learning.**

Built for the University of Cyprus EPL428 Internet of Things course. The LilyGO T-Beam S3 Supreme reads motion from its QMI8658 inertial sensor, recognizes gestures with a local Random Forest model, and sends command messages to a Mac over Wi-Fi. No cloud ML service is required.

## Features

| Input | Mac action |
| --- | --- |
| Cursor mode | Move mouse pointer using wand motion |
| Swipe up | Scroll up |
| Swipe down | Scroll down |
| Double flick | Left click |
| Semicircle left | Previous tab |
| Idle | No action |
| BOOT button | Toggle CURSOR / GESTURES modes |

The OLED provides **Wi-Fi status, mode, READY / RECORDING / COOLDOWN indicators, a recording progress bar, and the most recent model prediction/confidence**.

## Architecture

```text
QMI8658 (accelerometer + gyroscope, 6 channels)
  -> ~2-second timestamped capture
  -> 256 x 6 resampling
  -> feature extraction
  -> 300-tree Random Forest on ESP32-S3
  -> gesture-to-command mapping + confidence gate
  -> Wi-Fi UDP
  -> Python receiver on macOS
  -> mouse / scrolling / tab actions
```

The model recognizes 5 classes: `double_flick`, `idle`, `semicircle_left`, `swipe_down`, `swipe_up`. The firmware targets 400 Hz sampling; real logging in this setup has shown approximately 344 Hz. Timestamp-aware resampling helps normalize variable sample counts. This is **Wi-Fi UDP, not Bluetooth HID**.

## Hardware

- LilyGO T-Beam S3 Supreme (ESP32-S3)
- QMI8658 accelerometer and gyroscope
- SH1106 OLED, 128 x 64 pixels
- USB data cable for firmware flashing
- Compatible battery/power supply for untethered use
- Mac and ESP32 on a network that permits device-to-device UDP traffic

## Software

- PlatformIO (Espressif32 + Arduino), configured by `platformio.ini`
- Python for the macOS receiver
- macOS Accessibility permission for the terminal/Python process, if actual mouse/keyboard control is needed

## Project files

```text
AI-Magic-Wand/
├── platformio.ini
├── src/main.cpp
├── include/
│   ├── rf_model_5gestures.h
│   ├── ml_400hz/features_400hz.h
│   └── wifi_secrets.example.h   # dummy settings only
├── python/receiver.py
├── tools/set_v4_stage.py       # see baseline warning in SETUP.md
├── README.md
├── SETUP.md
└── .gitignore
```

Additional verified training and evaluation scripts can be published if private training recordings, credentials, and temporary output are removed.

## Getting started

Read **[SETUP.md](SETUP.md)** for the detailed firmware, Wi-Fi, receiver, usage, and troubleshooting instructions.

1. Clone the repository and install PlatformIO in a Python environment.
2. Copy `include/wifi_secrets.example.h` to **`include/wifi_secrets.h`**, then set your own Wi-Fi network, Mac IP, port, and token.
3. Configure the Mac receiver to use the same token; follow its actual configuration mechanism.
4. Connect the ESP32 by USB and upload the firmware.
5. Unplug USB and power the wand from a suitable battery.
6. Start the receiver in **dry-run** mode, confirm expected UDP commands, and only then opt in to `--live`.

```bash
python -m platformio run
python -m platformio run -t upload
python -u python/receiver.py --esp-ip <ESP32_CURRENT_IP>
# Only after safe tests:
python -u python/receiver.py --esp-ip <ESP32_CURRENT_IP> --live
```

The receiver defaults to dry run, which **prints commands but does not control the Mac**. Stop with `Ctrl+C`.

## Staged permissions

| Stage | Commands allowed |
| --- | --- |
| `safe` | None |
| `cursor` | MOVE |
| `scroll` | MOVE + SCROLL_UP + SCROLL_DOWN |
| `tabs` | Above + PREV_TAB |
| `click` | Above + LEFT_CLICK |

**Caution:** `tools/set_v4_stage.py` was designed to regenerate `src/main.cpp` from a local baseline. Make sure it uses the approved current baseline before switching stages. A fresh public clone will not have private backups; the baseline must be made independently distributable for this command to work out of the box. See SETUP.md.

## Model evaluation

An earlier limited on-device safe validation reported **34/40 accepted-correct active gestures**, with **0 false accepted active-gesture actions in that particular 50-trial session**. This is not a guarantee of future precision, and differs from offline model test-set accuracy. Final labeled evaluation should report classification accuracy, per-class recall, command acceptance, false activations, and wireless responsiveness separately.

## Safety & privacy

- Do **not** commit `include/wifi_secrets.h`, shared tokens, passwords, credentials, raw personal recordings, or historical backups.
- Test in dry-run and restrictive command stages before enabling live cursor/keyboard actions.
- Run UDP control on a **trusted private network** only; do not expose the port publicly. The custom token protocol is not encrypted secure transport.
- Gestures can be misclassified. Avoid sensitive/destructive windows during testing.
- A public repository does not automatically grant others permission to reuse code; add a suitable LICENSE once you've reviewed any third-party code and assets.

## Project context

Course project for **EPL428 — Internet of Things**, University of Cyprus. Focus: IoT device sensing, time-series feature extraction, embedded machine learning, Wi-Fi communication and practical human-computer interaction.
