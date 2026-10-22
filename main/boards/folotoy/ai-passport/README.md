<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# FoloToy AI Passport (ai-passport)

Board definition that lets the XiaoZhi voice assistant run on the
[FoloToy AI Passport](https://github.com/FoloToy/ai-passport) wearable.

## Hardware

- MCU: ESP32-C3, 8 MB flash, **no PSRAM**, USB Serial/JTAG console
- Audio: ES8311 codec (I2C 0x18) on the shared I2C bus, I2S full-duplex
  (amplifier enable not wired, treated as always on)
- Display: ST7789 (ST7789P3) 240x320 portrait, 4-line SPI
- Battery: CW2017 fuel gauge (I2C 0x63, optional)
- Buttons: UP / DOWN / OK share GPIO0 (ADC1_CH0) through a resistor ladder

Pin mapping follows `ai-passport/components/bsp/include/bsp_pins.h`:

| Function | Pin |
| --- | --- |
| LCD MOSI / SCLK / CS / DC | 9 / 8 / 1 / 20 |
| LCD backlight (PWM) | 21 |
| I2S MCLK / BCLK / WS / DOUT / DIN | 6 / 5 / 3 / 2 / 4 |
| I2C SDA / SCL | 10 / 7 |
| Buttons (ADC ladder) | GPIO0 |

## Build

```sh
python scripts/build.py folotoy/ai-passport --name ai-passport
```

Outputs `build/merged-binary.bin` (8 MB flash, USB Serial/JTAG console).

## Controls

The three physical keys map to XiaoZhi's voice-assistant actions:

- **OK** — single click: toggle the chat state (or enter Wi-Fi config mode
  while starting up)
- **UP** — single click: volume +10; long press: max volume
- **DOWN** — single click: volume -10; long press: mute

Because the ladder shares one ADC pin, XiaoZhi reads it as three
independent ADC buttons (the same pattern as the ESP-BOX-Lite).

## Eye companion interface

The portrait screen uses a pair of rounded, glowing eyes on a dark background.
Idle eyes breathe, glance and blink; listening eyes open attentively; recognized
user speech triggers an upward, amber thinking glance; answering eyes pulse.
Happy cloud emotions produce mint smiling arches, and errors produce a soft
coral expression. Temporary emotions return to the conversation state after
three seconds. These animations use LVGL shapes rather than decoded video.

Subtitles wrap in a fixed area below the eyes. Overflow scrolls vertically at
20 pixels per second, with a pause at each end. The last reply remains readable
after the conversation ends and resets when the next conversation starts.
Existing Chinese assets and dynamic glyph support remain in use.

After 60 seconds of idle time, the LCD and backlight turn off and eye animation
stops. Active conversations, provisioning, updates and the first reading pass
of a long subtitle keep the screen on. Any button wakes the screen and performs
its normal action in the same press; OK starts a conversation. Brightness is
restored from the existing setting. Screen-off does not disconnect Wi-Fi or
erase configuration. This ESP32-C3 build uses the OK button instead of voice
wake-up.

## Notes / calibration

- Display orientation, color inversion and the backlight PWM polarity were
  taken from the Passport BSP; verify on real hardware and adjust the
  `DISPLAY_*` macros in `config.h` if the image is rotated/inverted or the
  backlight is reversed.
- The ADC button voltage windows assume the Passport's external 10 kOhm
  pull-up. Re-measure with the Button page if thresholds drift.
- CW2017 presence is optional; without the chip the status bar shows no
  battery level and the board keeps working.
