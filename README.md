# Odtwarzacz MP3
## Overview
[PL] Projekt został stworzony w języku C z wykorzystaniem płytki LPC 1769. Celem projektu jest podstawowa funkcjonalność odtwarzania muzyki w formacie WAV zapisanej na karcie SD. Projekt obejmuje odczytywanie plików z zewnętrznego nośnika, wyświetlanie komunikatów na ekranie OLED, oraz odtwarzanie dekodowanego dźwięku przez głośnik.<br />
[EN] The project was developed in C using the LPC 1769 board. The goal of the project is to provide basic functionality for playing WAV music stored on an SD card. The project includes reading files from an external storage medium, displaying messages on an OLED screen, and playing the decoded sound through a speaker.

## Version
[PL] Aktualna wersja: 1.0<br />
[EN] Current version: 1.0

## Project Status
[x] Turning the player On/Off with a button (GPIO)<br />
[x] Loading files from SD Memory Card using SPI<br />
[x] Functional OLED display showing track list from SD<br />
[x] DAC converter<br />
[x] Timer and system interrupts<br />
[x] Amplifier control with a knob<br />
[x] Volume visualization using a LED Bar and I2C as the interface

## Additional notes
- The project was developed using MCUXpresso IDE<br />
- .hex file has been added
