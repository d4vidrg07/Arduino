# Arduino

Embedded programming on the **ATmega2560** (Arduino Mega) written for the Peripherals and Interfaces labs at ULPGC. The code works directly with the microcontroller registers (ports, timers and interrupts) instead of relying only on Arduino library functions.

## Projects

| Sketch | What it does |
|---|---|
| [`plab2_frequency_meter`](plab2_frequency_meter) | Lab 2: timers on the ATmega2560. A 0-999 counter on a 4-digit 7-segment display, a frequency meter based on input capture and PWM outputs. |

## Lab 2 in detail

- **Display and keypad multiplexing:** the Timer 3 compare interrupt (CTC mode, every 5 ms) lights one digit and reads one keypad column on each pass, so the four digits look lit at once.
- **Counter (modes 1-3):** shown in three display layouts (3 digits, lower 2 digits, upper 2 digits). It is changed with push buttons (step of 1 or 2) or by typing a number on the keypad (`#` accepts, `*` cancels). A buzzer beeps each time the value changes.
- **Frequency meter (mode 4):** measures the period between two rising edges on `ICP3` with the Timer 3 input capture, counting timer overflows to reach about 1 Hz. The result (up to 9999 Hz) is shown on the display and printed in the terminal.
- **PWM with Timer 4 (modes 5 and 6):** three outputs (OC4A, OC4B, OC4C) at 50 %, 10 % and 25 %, with the duty cycle of OC4B and OC4C adjustable from the push buttons, and the direction of a motor controlled with a push button.
- **Serial menu:** the mode (1-6) is selected by typing its number in the serial terminal.

## Hardware and tools

- ATmega2560 (Arduino Mega 2560)
- 4-digit 7-segment display, 4x3 keypad, push buttons and a speaker
- Arduino IDE; the circuit can also be simulated in Proteus
- Serial terminal at 9600 baud

## Run

1. Open `plab2_frequency_meter/plab2_frequency_meter.ino` in the Arduino IDE.
2. Select the board *Arduino Mega or Mega 2560* and upload.
3. Open the serial monitor at 9600 baud and choose a mode from the menu.
