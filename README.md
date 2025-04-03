# Odtwarzacz MP3
## Overview
[PL] Projekt został stworzony w języku C z wykorzystaniem płytki LPC 1769. Celem projektu jest podstawowa funkcjonalność odtwarzania muzyki w formacie MP3 zapisanej na karcie SD. Projekt obejmuje odczytywanie plików z zewnętrznego nośnika, wyświetlanie komunikatów na ekranie OLED, oraz odtwarzanie dekodowanego dźwięku przez głośnik.
### Aktualna wersja: **v0.0.1-alpha**
[EN] The project was developed in C using the LPC 1769 board. The goal of the project is to provide basic functionality for playing MP3 music stored on an SD card. The project includes reading files from an external storage medium, displaying messages on an OLED screen, and playing the decoded sound through a speaker.
### Current version: **v0.0.1-alpha**

## Project Status
- [ ] Turning the player on and off with a button (experimental)
- [x] OLED display + SPI/F + displaying track list
- [x] SD Memory card + SPI/F
- [ ] DAC converter + timer + interrupts (MP3 files preparation)
- [ ] Amplifier control
- [ ] Amplifier knob
- [ ] Volume visualization on a LED bar + I2C as the display interace

### Additional notes
- the project was developed using **MCUXpresso IDE**
- current main.c is located in mmc_fat folder
