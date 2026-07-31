# nRF52 LED Games

Interactive LED and button games built for the Nordic nRF52 DK (`nrf52dk_nrf52832`), using the nRF Connect SDK (Zephyr RTOS). No external hardware required — everything runs on the board's built-in buttons and LEDs.

## Overview

This project turns the DK's four onboard buttons and four onboard LEDs into two interactive modes:

1. **Idle Mode** — each button independently controls its own LED. Holding a button causes its LED to smoothly "breathe" (fade in and out) rather than simply switching on and off. Multiple buttons can be held at once, each driving its own LED independently.

2. **Memory Game Mode** — a Simon-style challenge. Holding all four buttons simultaneously for three seconds triggers a "Get Ready" countdown, after which the board begins flashing a randomly generated sequence of LEDs. The player must repeat the sequence by pressing the corresponding buttons in order. Each successful round extends the sequence by one additional step. An incorrect button press, or failing to respond within the time limit, ends the game with a visual "game over" signal, after which the board returns to Idle Mode.

## Technical Features

- **GPIO interrupts** for button input, with software debouncing to prevent duplicate registrations from a single press.
- **PWM-driven LED dimming**, enabling smooth brightness transitions instead of binary on/off states.
- **Hardware-based random number generation**, using the SoC's onboard entropy source to generate genuinely random LED sequences.
- **A finite state machine** governing game flow (`IDLE` → `GET_READY` → `SHOW_PATTERN` → `WAIT_INPUT` → `ROUND_COMPLETE` → `GAME_OVER`), ensuring predictable, race-condition-free transitions between modes.
- **A devicetree overlay** extending PWM output to all four onboard LEDs, since the board's default configuration only wires PWM to LED 1.

## Hardware

No external wiring is required. This project uses only the nRF52 DK's onboard buttons (Button 1–4) and onboard LEDs (LED 1–4).

## Project Structure
├── CMakeLists.txt
├── prj.conf
├── app.overlay
└── src/
└── main.c

## Configuration

`prj.conf` enables:
- `CONFIG_GPIO` — button input handling
- `CONFIG_PWM` — LED dimming/breathing effect
- `CONFIG_ENTROPY_GENERATOR` — hardware random number generation for the memory game

`app.overlay` extends the PWM peripheral's pin configuration to drive LED 2, 3, and 4 in addition to the board's default PWM-enabled LED 1, using a shared `pwm0` instance across all four output channels.

## Building and Flashing

```bash
west build -b nrf52dk_nrf52832 -p always
west flash
```

## How to Play

**Idle Mode:** Press and hold any button — its corresponding LED will breathe for as long as the button is held.

**Starting the Memory Game:** Hold all four buttons simultaneously for three seconds. All four LEDs will flash together as a "Get Ready" signal — release the buttons at this point.

**Playing:** The board flashes a sequence of LEDs one at a time. Repeat the sequence by pressing the matching buttons in the same order. Each correct round adds one more step to the sequence.

**Game Over:** An incorrect press, or a timeout while waiting for input, ends the round. All LEDs flash rapidly several times before the board returns to Idle Mode.

## Built With

- [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) (Zephyr RTOS)
- Nordic nRF52 DK (`nrf52832`)
