# Setup and operating instructions

## 1. Requirements

- LilyGO T-Beam S3 Supreme, USB data cable, suitable power source/battery
- Mac with Python 3 and internet access to install dependencies
- Same trusted Wi-Fi network for Mac and ESP32, with no client isolation

Install PlatformIO in a virtual environment:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install platformio
```

If you publish a tested `requirements.txt` for your Mac receiver, install it too. Otherwise, inspect `python/receiver.py` imports and install the required libraries; do not assume PlatformIO installs the Mac-side packages.

## 2. Private configuration

Copy the example file:

```bash
cp include/wifi_secrets.example.h include/wifi_secrets.h
```

Edit **only the local** `wifi_secrets.h` with your Wi-Fi SSID, password, current Mac IP and shared token as defined in the firmware. Make sure the Mac receiver uses the same token. Do not publish actual credentials.

Find your Mac Wi-Fi IP using `ipconfig getifaddr en0` if `en0` is your Wi-Fi interface. ESP32 may receive a different IP each boot; read its serial log and update the receiver allowlist.

## 3. Stage-script baseline warning

The existing `tools/set_v4_stage.py` was written to create staged firmware from a baseline. If that baseline is under `backups/`, **it will not be included in the public repository**. Before claiming clean-clone support, change the script to use a deliberately committed, verified safe baseline (e.g. `firmware_baselines/main_v4_4_safe.cpp`) and test a fresh clone. Until then, publish it as a maintenance tool, not a guaranteed one-command setup.

Choose and document a safe default state before publishing. Verify your active firmware stage with:

```bash
grep -n -E 'V4 LIVE STAGE|V4 SAFE DRY-RUN|NEVER SEND UDP|REARM_MS|READY: MOVE' src/main.cpp
```

`click` stage needs explicit `--confirm-clicks` when using the staged helper. Running a stage helper **does not flash the device**; rebuild and upload afterward.

## 4. Build and flash firmware

Connect the wand via USB. From the repository root:

```bash
python -m platformio device list
python -m platformio run
python -m platformio run -t upload
```

If automatic port selection fails:

```bash
python -m platformio run -t upload --upload-port /dev/cu.usbmodemXXXX
```

Use the actual discovered port instead of `XXXX`. Close the Serial Monitor before uploading. USB flashing will fail while the wand is disconnected even if the device works wirelessly.

Serial Monitor (optional):

```bash
python -m platformio device monitor -b 115200
```

## 5. Receiver

With both devices connected to the same Wi-Fi, start in **dry-run** mode:

```bash
python -u python/receiver.py --esp-ip <CURRENT_ESP32_IP>
```

After confirming the intended commands, stop with `Ctrl+C` and enable actual Mac actions:

```bash
python -u python/receiver.py --esp-ip <CURRENT_ESP32_IP> --live
```

The Mac may require permission at **System Settings > Privacy & Security > Accessibility** for Terminal or the Python process.

## 6. Use the wand

1. Turn on and verify `WiFi: CONNECTED` on the OLED.
2. Press **BOOT** to toggle between CURSOR and GESTURES modes.
3. In GESTURES, wait for **READY: MOVE!** before performing one gesture.
4. Watch **RECORDING** for approximately two seconds. Wait for READY again before another gesture.
5. Gesture mapping: swipe up/down = scroll up/down; double flick = click; semicircle left = previous tab.

## 7. Troubleshooting

| Issue | Check |
| --- | --- |
| Upload says device absent | Connect USB, choose correct port, close Serial Monitor |
| Wi-Fi OFFLINE | Network, password, battery power |
| Mac receiver prints MOVE only | Switch to GESTURES mode and verify firmware stage |
| Commands printed but no actions | Receiver may be in dry-run; verify `--live` and macOS Accessibility |
| OLED recognizes gesture but nothing sent | Confidence thresholds, allowed commands, network/token consistency |
| Stage switch reverts UI improvements | Baseline used by stage script is outdated |
| Wireless stops after reconnect | ESP32 IP may have changed; check hotspot client isolation |

## 8. Public release checklist

- [ ] Real `include/wifi_secrets.h` excluded and never committed
- [ ] Example Wi-Fi file uses dummy values, not real tokens
- [ ] No credentials in any tracked source, docs, or Git history
- [ ] No personal data, raw recordings, locally identified logs, or backups included
- [ ] Stage script baseline works from a fresh clone **or limitation documented**
- [ ] Tested build, upload, and receiver install on a clean environment
- [ ] Checked all third-party assets and licenses
