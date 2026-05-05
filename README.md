# Klock
**A minimal desk clock. Three modes. One button.**

![Klock](https://www.instructables.com/assets/img/instructables-logo-v2.png)

> *Only what's needed.*

ESP32 + 12-LED WS2812B NeoPixel ring. Syncs to NTP over Wi-Fi. Automatically switches between warm and cool color themes at sunrise and sunset.

---

## Modes

**Clock** — Three hands on a 12-LED ring. Hour, minute, and second hands glide continuously using sub-pixel blending. Overlapping hands blend colors additively. Unlit LEDs are fully off.

**Solar / Lunar Clock** — The bottom six LEDs represent the horizon. A single pixel tracks the sun during the day and the moon at night, moving east to west based on your real local sunrise and sunset times.

**Timer** — Single-click adds one minute (up to 12). The ring breathes while you set it. Long-press starts the countdown. LEDs extinguish one per minute. Shifts to red at 3 minutes remaining. Three-beep alarm at zero.

---

## Controls

| Action | Result |
|---|---|
| Single click | Add 1 minute (timer mode only) |
| Double click | Cycle to next mode |
| Long press | Start timer countdown |

---

## Hardware

- ESP32 DevKit (WROOM-32)
- 12-LED WS2812B NeoPixel Ring
- Passive buzzer
- Momentary push button

---

## Build Guide

Full step-by-step instructions, wiring, and enclosure build at:
**[instructables.com/Klock-a-Minimal-Diy-Desk-Clock](https://www.instructables.com/Klock-a-Minimal-Diy-Desk-Clock/)**

---

## License

MIT
