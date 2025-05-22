/*****************************************************************************
 *   Odtwarzacz WAV, Systemy Wbudowane 2024/2025, DUZY PROJEKT
 *
 *   Zadaniem algorytmu jest odczytanie plików WAV z karty SD,
 *   oraz uruchomienie ich na głośniku.
 *
 *   Copyright(C) 2025, Maja Binkowska, Bartosz Łężniak, Paweł Rajnert
 *   All rights reserved.
 *
 ******************************************************************************/

#include "lpc17xx_pinsel.h"   /* ustwaienie pinów */
#include "lpc17xx_gpio.h"     /* GPIO */
#include "lpc17xx_ssp.h"      /* SPI, uzywane do karty SD */
#include "lpc17xx_timer.h"    /* timer do czasu */
#include "lpc17xx_dac.h"      /* przetwornik DAC */
#include "lpc17xx_i2c.h"      /* I2C do komunikacji z wyswietlaczem OLED i linijką diodową */
#include "stdio.h"            /* biblioteka We/Wy */
#include "lpc17xx_adc.h"
#include <stdlib.h>

#include "diskio.h"           /* wczytana biblioteka do obslugi FAT i karty SD */
#include "ff.h"

#include "oled.h"             /* wczytana biblioteka do obslugi ekranu OLED */
#include <stdbool.h>
#include <string.h>

#define WAV_BUF_SIZE 512

/* Pinout dla LEDów (linijka diodowa) */
#define I2C_LED_EXPANDER_ADDR 0x20  // Adres expandera I2C dla linijki diodowej

/* Pinout dla enkodera obrotowego */
#define ROT_A_PORT 2
#define ROT_A_PIN 0
#define ROT_B_PORT 2
#define ROT_B_PIN 1

/* zapisywanie danych o znalezionych plikach na karcie SD */
#define MAX_FILES 9
#define MAX_FILENAME_LEN 64

#define SEKUNDA 1000000

typedef struct {
    bool isPlaying;
    int currentTrack;
    uint32_t volume;
    bool screenState;
    int fileCount;
    char fileList[MAX_FILES][MAX_FILENAME_LEN];
    FIL currentFile;
    uint8_t wavBuf[WAV_BUF_SIZE];
    uint32_t sampleRate;
    uint32_t dataSize;
    uint16_t numChannels;
    uint32_t delay;
    uint32_t bufferPos;           // NOWE: pozycja w buforze
    uint32_t remainingData;       // NOWE: pozostałe dane do odtworzenia
    bool needNewBuffer;           // NOWE: flaga potrzeby nowego bufora
} PlayerState;

// Inicjalizacja z nowymi polami
PlayerState player = {
    .isPlaying = false,
    .currentTrack = 0,
    .volume = 50,
    .screenState = true,
    .fileCount = 0,
    .sampleRate = 0,
    .dataSize = 0,
    .numChannels = 0,
    .delay = 0,
    .bufferPos = 0,
    .remainingData = 0,
    .needNewBuffer = false
};

static FILINFO Finfo;      /* zasob przechowujacy informacje o plikach z karty SD */
static FATFS Fatfs[1];     /* implementacja fatfs do wczytywania zasobu FAT */
//static uint8_t buf[64];    /* bufor pomocniczy do wyświetlania */

/* Licznik milisekund dla systemu */
static volatile uint32_t msTicks = 0;

/* Deklaracje funkcji */
static void init_ssp(void);
static void init_i2c(void);
static void init_adc(void);
static void button_init(void);
static void rotary_init(void);
static void led_bar_init(void);
static void led_bar_set(uint8_t value);
static void display_files(void);
static void play_wav_file(const char* filename);
static void stop_wav(void);
static void next_track(void);
//static void prev_track(void);
static void set_volume(uint32_t vol);
static uint32_t getTicks(void);

DWORD get_fattime(void)
{
    /* Zwraca stały czas: 2025-05-06, 12:00:00 */
    return ((DWORD)(2025 - 1980) << 25)    /* Rok */
         | ((DWORD)5 << 21)                /* Miesiąc */
         | ((DWORD)6 << 16)                /* Dzień */
         | ((DWORD)12 << 11)               /* Godzina */
         | ((DWORD)0 << 5)                 /* Minuta */
         | ((DWORD)0 >> 1);                /* Sekunda */
}

static uint32_t getTicks(void)
{
    return msTicks;
}

/* Handler przerwania systemowego - inkrementuje licznik milisekund */
void SysTick_Handler(void) {
    msTicks++;  // Inkrementacja licznika milisekund
    disk_timerproc();
}

static void init_ssp(void)		/* inicjalizacja interfejsu SPI */
{
	SSP_CFG_Type SSP_ConfigStruct;		/* deklaracja struktury konfiguracyjej dla SPI */
	PINSEL_CFG_Type PinCfg;				/* i pinĂłw */

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK (zegar SPI)
	 * P0.8 - MISO (dane odczytywane)
	 * P0.9 - MOSI (dane wysyĹ‚ane)
	 * P2.2 ustawiony jako GPIO dla SSEL (chip select)
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);		/* inicjalizuje SPI i wĹ‚Ä…cza go (kod do koĹ„ca) */

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}


static void init_i2c(void) {
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_adc(void)
{
    PINSEL_CFG_Type PinCfg;

    /*
     * Inicjalizacja ADC i laczenie pinow
     * AD0.0 na P0.23 */
    PinCfg.Funcnum = 1;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = 23;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);

    /* Konfiguracja ADC:
     * Częstotliwość 0.2Mhz
     * ADC kanał 0, bez przerwań
     */
    ADC_Init(LPC_ADC, 200000);
    ADC_IntConfig(LPC_ADC,ADC_CHANNEL_0,DISABLE);
    ADC_ChannelCmd(LPC_ADC,ADC_CHANNEL_0,ENABLE);
}

static void init_dac(void) {
    PINSEL_CFG_Type PinCfg;

    // AOUT na P0.26 jako DAC
    PinCfg.Funcnum = 2;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = 26;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);

    // Inicjalizacja DAC
    DAC_Init(LPC_DAC);

    // Konfiguracja wyjść wzmacniacza LM4811 (CLK, UP/DN, SHUTDN)
    GPIO_SetDir(0, 1UL << 27, 1); // CLK
    GPIO_SetDir(0, 1UL << 28, 1); // UP/DN
    GPIO_SetDir(2, 1UL << 13, 1); // SHUTDN

    // Wzmacniacz aktywny: SHUTDN = 0
    GPIO_ClearValue(0, 1UL << 27);
    GPIO_ClearValue(0, 1UL << 28);
    GPIO_ClearValue(2, 1UL << 13);
}


static void button_init(void)
{
    /* P0.4 jako wejscie GPIO - główny przycisk zasilania */
    PINSEL_CFG_Type PinCfg;
    PinCfg.Funcnum = 0;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0; // pullup
    PinCfg.Pinnum = 4;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);

    /* ustaw P0.4 jako input */
    GPIO_SetDir(0, 1 << 4, 0);

    /* P0.5 jako wejście GPIO - przycisk play/pause */
    PinCfg.Pinnum = 5;
    PINSEL_ConfigPin(&PinCfg);
    GPIO_SetDir(0, 1 << 5, 0);

    /* P0.6 jako wejście GPIO - przycisk next */
    PinCfg.Pinnum = 6;
    PINSEL_ConfigPin(&PinCfg);
    GPIO_SetDir(0, 1 << 6, 0);
}

void TIMER1_IRQHandler(void) {
    if (TIM_GetIntStatus(LPC_TIM1, TIM_MR0_INT)) {

        if (player.isPlaying && player.bufferPos + 1 < WAV_BUF_SIZE) {
            int16_t pcm_sample = (int16_t)(player.wavBuf[player.bufferPos] |
                                          (player.wavBuf[player.bufferPos + 1] << 8));

            // Wzmocnienie sygnału PCM (+20%)
            int32_t amplified = pcm_sample * 6 / 5;
            if (amplified > 32767) amplified = 32767;
            if (amplified < -32768) amplified = -32768;

            uint32_t unsigned_sample = (uint32_t)(amplified + 32768);
            unsigned_sample = (unsigned_sample * player.volume) / 100;

            uint16_t dac_value = (unsigned_sample * 1023) / 65535;
            DAC_UpdateValue(LPC_DAC, dac_value);

            player.bufferPos += 2;

            // Gdy zużyto pierwszą połowę bufora — przygotuj nową
            if (player.bufferPos == WAV_BUF_SIZE / 2) {
                player.needNewBuffer = true;
            }

        } else {
            // Koniec danych w buforze
            DAC_UpdateValue(LPC_DAC, 512);

            if (player.isPlaying) {
                player.bufferPos = 0;
                player.needNewBuffer = true;
            }
        }

        TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
    }
}

static void init_Timer(void)
{
    TIM_TIMERCFG_Type Config;
    TIM_MATCHCFG_Type Match_Cfg;

    Config.PrescaleOption = TIM_PRESCALE_USVAL;
    Config.PrescaleValue = 1; // 1 mikrosekunda

    // Wyłącz timer przed konfiguracją
    TIM_Cmd(LPC_TIM1, DISABLE);

    // Inicjalizuj timer
    TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &Config);

    // Konfiguracja Match dla 8kHz
    Match_Cfg.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
    Match_Cfg.IntOnMatch = TRUE;
    Match_Cfg.ResetOnMatch = TRUE;
    Match_Cfg.StopOnMatch = FALSE;
    Match_Cfg.MatchChannel = 0;

    // Dla 8kHz: 1000000/8000 = 125 mikrosekund
    Match_Cfg.MatchValue = 125;

    TIM_ConfigMatch(LPC_TIM1, &Match_Cfg);

    // Włącz przerwania
    NVIC_EnableIRQ(TIMER1_IRQn);

    // Włącz timer
    TIM_Cmd(LPC_TIM1, ENABLE);
}

static void rotary_init(void)
{
    PINSEL_CFG_Type PinCfg;
    PinCfg.Funcnum = 0;        // GPIO
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;

    // ROT_A
    PinCfg.Portnum = ROT_A_PORT;
    PinCfg.Pinnum = ROT_A_PIN;
    PINSEL_ConfigPin(&PinCfg);

    // ROT_B
    PinCfg.Portnum = ROT_B_PORT;
    PinCfg.Pinnum = ROT_B_PIN;
    PINSEL_ConfigPin(&PinCfg);

    // Set as input
    GPIO_SetDir(ROT_A_PORT, (1 << ROT_A_PIN), 0);
    GPIO_SetDir(ROT_B_PORT, (1 << ROT_B_PIN), 0);
}

static void led_bar_init(void)
{
    uint8_t config_data[2];
    I2C_M_SETUP_Type i2cSetup;

    // Sprawdzenie czy ekspander I2C jest dostępny
    // Przed próbą inicjalizacji, dodajemy drobne opóźnienie
    Timer0_us_Wait(10000); // 10 ms

    /* Konfiguracja expandera I/O jako wyjścia dla linijki diodowej */
    // tu bylo config_data[0] = 0x00
    config_data[0] = 0x03;  // Rejestr konfiguracji
    config_data[1] = 0x00;  // Wszystkie piny jako wyjścia (0)

    i2cSetup.sl_addr7bit = I2C_LED_EXPANDER_ADDR;
    i2cSetup.tx_data = config_data;
    i2cSetup.tx_length = 2;
    i2cSetup.rx_data = NULL;
    i2cSetup.rx_length = 0;
    i2cSetup.retransmissions_max = 3;

    // Obsługa ewentualnego błędu komunikacji z ekspanderem
    if (I2C_MasterTransferData(LPC_I2C2, &i2cSetup, I2C_TRANSFER_POLLING) != SUCCESS) {
        // Jeśli komunikacja się nie powiedzie, wyświetl informację
        oled_putString(1, 45, (uint8_t*)"LED I2C Error", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }
}

static void led_bar_set(uint8_t value)
{
    uint8_t led_data[2];
    I2C_M_SETUP_Type i2cSetup;

    /* Konwersja wartości głośności (0-100) na wzór linijki (8 LEDów) */
    uint8_t led_pattern = 0;

    /* Poprawiona logika dla LED Bar */
    if (value >= 99) {
        // Jesli glosnosc >= 99%, zapal wszystkie 8 LEDów
        led_pattern = 0xFF; // 11111111
    } else {
        // Oblicz liczbe LEDów do zapalenia na podstawie glosnosci
        uint8_t leds_to_light = (value * 8) / 100;

        // Zapal odpowiednia liczbe LEDów od prawej strony
        for (int i = 0; i < leds_to_light; i++) {
            led_pattern |= (1 << i);
        }
    }

    /* Wysyłanie wzoru do expandera I/O */
    led_data[0] = 0x01;  // Rejestr wyjścia
    led_data[1] = led_pattern;

    i2cSetup.sl_addr7bit = I2C_LED_EXPANDER_ADDR;
    i2cSetup.tx_data = led_data;
    i2cSetup.tx_length = 2;
    i2cSetup.rx_data = NULL;
    i2cSetup.rx_length = 0;
    i2cSetup.retransmissions_max = 3;

    // Ignorowanie błędów komunikacji I2C z linijką diodową
    // Jeśli komunikacja się nie powiedzie, po prostu kontynuuj
    I2C_MasterTransferData(LPC_I2C2, &i2cSetup, I2C_TRANSFER_POLLING);
}

/* Wyświetlanie listy plików */
static void display_files(void)
{
    /* Sprawdź czy ekran jest włączony */
    if (!player.screenState) {
        return;
    }

    oled_clearScreen(OLED_COLOR_WHITE);

    /* Oznaczenie aktualnie wybranego pliku */
    for (int i = 0; i < player.fileCount; i++) {
        if (i == player.currentTrack) {
            /* Wyróżnienie aktualnego utworu */
            oled_putString(1, 1 + (i * 8), (uint8_t*)"> ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            oled_putString(10, 1 + (i * 8), (uint8_t*)player.fileList[i], OLED_COLOR_WHITE, OLED_COLOR_BLACK);
        } else {
            oled_putString(1, 1 + (i * 8), (uint8_t*)"  ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            oled_putString(10, 1 + (i * 8), (uint8_t*)player.fileList[i], OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        }
    }

    /* Wyświetlenie statusu odtwarzania */
    char status_str[16];
    sprintf(status_str, "Vol: %lu%%  %s", player.volume, player.isPlaying ? "PLAY" : "STOP");
    oled_putString(1, 55, (uint8_t*)status_str, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
}

/* Zatrzymanie odtwarzania */
static void stop_wav(void)
{
    if (player.isPlaying) {
        player.isPlaying = false;
        f_close(&player.currentFile);

        // Wyzerowanie DAC (cisza)
        DAC_UpdateValue(LPC_DAC, 512);

        // Aktualizacja wyświetlacza
        display_files();
    }
}

static void play_wav_file(const char* filename)
{
    FRESULT fr;
    UINT br;
    uint8_t hdr[44];
    char tmp_str[64];

    // Zatrzymaj obecne odtwarzanie
    stop_wav();

    // Otwórz plik WAV
    fr = f_open(&player.currentFile, filename, FA_READ);
    if (fr != FR_OK) {
        sprintf(tmp_str, "Open err: %d", fr);
        oled_putString(1, 45, (uint8_t*)tmp_str, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return;
    }

    // Przeczytaj nagłówek WAV (44 bajty)
    f_read(&player.currentFile, hdr, 44, &br);
    if (br != 44 || hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F') {
        oled_putString(1, 45, (uint8_t*)"Invalid WAV", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        f_close(&player.currentFile);
        return;
    }

    // Wyciągnij parametry z nagłówka
    player.sampleRate = hdr[24] | (hdr[25]<<8) | (hdr[26]<<16) | (hdr[27]<<24);
    player.dataSize = hdr[40] | (hdr[41]<<8) | (hdr[42]<<16) | (hdr[43]<<24);
    player.numChannels = hdr[22] | (hdr[23] << 8);

    // Sprawdź czy format jest obsługiwany
    if (player.numChannels != 1) {
        oled_putString(1, 45, (uint8_t*)"Only mono supported", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        f_close(&player.currentFile);
        return;
    }

    if (player.sampleRate != 8000) {
        sprintf(tmp_str, "Unsupported SR: %lu", player.sampleRate);
        oled_putString(1, 45, (uint8_t*)tmp_str, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        f_close(&player.currentFile);
        return;
    }

    // Wyświetl informacje o pliku
    sprintf(tmp_str, "8kHz Mono, Size: %lu", player.dataSize);
    oled_putString(1, 36, (uint8_t*)tmp_str, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    // Inicjalizuj parametry odtwarzania
    player.remainingData = player.dataSize;
    player.bufferPos = 0;
    player.needNewBuffer = true;

    // Załaduj pierwszy bufor
    UINT toRead = (player.remainingData > WAV_BUF_SIZE) ? WAV_BUF_SIZE : player.remainingData;
    fr = f_read(&player.currentFile, player.wavBuf, toRead, &br);
    if (br > 0) {
        player.remainingData -= br;
        player.bufferPos = 0;
        player.needNewBuffer = false;
        player.isPlaying = true;


        oled_putString(1, 45, (uint8_t*)"Playing...", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    } else {
        oled_putString(1, 45, (uint8_t*)"Read error", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        f_close(&player.currentFile);
    }
}

/* Uruchomienie kolejnego utworu */
static void next_track(void)
{
    if (player.fileCount == 0) return;

    player.currentTrack = (player.currentTrack + 1) % player.fileCount;
    display_files();
}

///* Uruchomienie poprzedniego utworu */
//static void prev_track(void)
//{
//    if (player.fileCount == 0) return;
//
//    player.currentTrack = (player.currentTrack + player.fileCount - 1) % player.fileCount;
//    display_files();
//}

/* Ustawienie głośności */
static void set_volume(uint32_t vol)
{
    /* Ograniczenie wartości głośności do zakresu 0-100% */
    if (vol > 100) vol = 100;

    player.volume = vol;

    /* Aktualizacja linijki diodowej */
    led_bar_set(player.volume);

    /* Aktualizacja wyświetlacza */
    display_files();
}

int main (void) {
    DSTATUS stat;   /* status inicjalizacji karty SD */
    FRESULT fr;     /* wynik operacji na pliku */
    DIR dir;        /* wczytany katalog z karty SD */
//    UINT br;        /* liczba przeczytanych bajtów */
//    char path[64];  /* ścieżka do pliku */
//	PINSEL_CFG_Type PinCfg;
	
    SystemInit();   
    SysTick_Config(SystemCoreClock / 1000);    // 1ms ticks
    Timer0_us_Wait(100000); // 100 ms Krótkie opóźnienie dla stabilizacji systemów
	
    init_ssp();     /* SPI - komunikacja z karta SD */
    init_i2c();     /* I2C - komunikacja z OLED i linijką diodową */
    init_adc();     /* ADC - wejście analogowe (potencjometr) */
    button_init();  /* Inicjalizacja przycisków */
    rotary_init();  /* Inicjalizacja enkodera obrotowego */
	init_dac();
	init_Timer();
    Timer0_us_Wait(SEKUNDA); // 1 sekunda
	
    oled_init();
    oled_clearScreen(OLED_COLOR_WHITE);
    oled_putString(1, 1, (uint8_t*)"WAV Player", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1, 10, (uint8_t*)"Inicjalizacja...", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    /* Inicjalizacja linijki diodowej */
    led_bar_init();

    /* Ustawienie początkowego poziomu głośności */
    set_volume(50);
    led_bar_set(player.volume);

    stat = disk_initialize(0);		/* inicjalizacja karty SD */

   	/* sprawdzamy czy karta SD jest dostÄ™pna */
       if (stat & STA_NOINIT) {
    	    oled_putString(1, 19, (uint8_t*)"not init.", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
       }

       if (stat & STA_NODISK) {
    	    oled_putString(1, 28, (uint8_t*)"no disc err", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
       }

    oled_clearScreen(OLED_COLOR_WHITE);
    /* Montowanie karty SD i inicjalizacja FAT */
    fr = f_mount(0, &Fatfs[0]);
    if (fr != FR_OK) {
        oled_putString(1, 20, (uint8_t*)"Blad montowania SD", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return 1;
    } else {
        oled_putString(1, 20, (uint8_t*)"SD OK", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* Otwarcie katalogu głównego */
    fr = f_opendir(&dir, "/");
    if (fr) {
        oled_putString(1, 30, (uint8_t*)"Blad otw. dir", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return 1;
    }

    /* Skanowanie plików WAV */
    player.fileCount = 0;
    while (player.fileCount < MAX_FILES) {
        /* Odczytanie kolejnego wpisu w katalogu */
        fr = f_readdir(&dir, &Finfo);
        if (fr != FR_OK || Finfo.fname[0] == 0) {
            break;  /* Koniec plików lub błąd */
        }

        /* Sprawdzenie czy to plik WAV */
        if (!(Finfo.fattrib & AM_DIR)) {
            char *ext = strrchr(Finfo.fname, '.');
            if (ext && (strcmp(ext, ".WAV") == 0 || strcmp(ext, ".wav") == 0)) {
                /* Zapisanie nazwy pliku na liście */
                strncpy(player.fileList[player.fileCount], Finfo.fname, MAX_FILENAME_LEN-1);
                player.fileList[player.fileCount][MAX_FILENAME_LEN-1] = '\0';
                player.fileCount++;
            }
        }
    }
    // f_closedir(&dir);

    /* Wyświetlanie informacji o liczbie znalezionych plików */
    char msg[32];
    sprintf(msg, "Znaleziono %d pliki WAV", player.fileCount);
    oled_putString(1, 30, (uint8_t*)msg, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	Timer0_us_Wait(100000); // 100 ms
    stop_wav();
    /* Wyświetlenie listy plików */
    display_files();

    /* Automatyczne odtwarzanie pierwszego pliku, jeśli jest dostępny */
    if (player.fileCount > 0) {
        player.currentTrack = 0;  // Wybierz pierwszy plik
        play_wav_file(player.fileList[player.currentTrack]);
    }

    /* Główna pętla programu */
    uint32_t lastBtnCheck = 0;
    uint8_t lastRotA = 0, lastRotB = 0;
    uint32_t rotLastChange = 0;

    while (1) {
        uint32_t currentTime = getTicks();

        /* Sprawdzanie przycisków co 50ms (debouncing) */
        if (currentTime - lastBtnCheck >= 50) {
            lastBtnCheck = currentTime;

            /* Przycisk Play/Pause */
            if (!(GPIO_ReadValue(0) & (1 << 5))) {
                if (player.isPlaying) {
                    stop_wav();
                } else if (player.fileCount > 0) {
                    play_wav_file(player.fileList[player.currentTrack]);
                }
                Timer0_us_Wait(200000); // 200 ms  Debouncing
            }

            /* Przycisk Next */
            if (!(GPIO_ReadValue(0) & (1 << 6))) {
                next_track();
                if (player.isPlaying) {
                    stop_wav();
                    play_wav_file(player.fileList[player.currentTrack]);
                }
                Timer0_us_Wait(200000); // 200 ms  Debouncing
            }
        }

        /* Sprawdzenie enkodera obrotowego (głośność) */
        uint8_t rotA = GPIO_ReadValue(ROT_A_PORT) & (1 << ROT_A_PIN);
        uint8_t rotB = GPIO_ReadValue(ROT_B_PORT) & (1 << ROT_B_PIN);

        /* Detekcja ruchu enkodera */
        if ((lastRotA != rotA) && currentTime - rotLastChange > 5) {
            rotLastChange = currentTime;

            if (rotA != rotB) {
                /* Zwiększenie głośności */
                if (player.volume < 99 ) {
                    set_volume(player.volume + 5);
                }
            } else {
                /* Zmniejszenie głośności */
                if (player.volume > 0) {
                    set_volume(player.volume - 5);
                }
            }
        }
        lastRotA = rotA;
        lastRotB = rotB;

        if (player.isPlaying && player.needNewBuffer && player.remainingData > 0) {
            UINT br;
            UINT toRead = (player.remainingData > WAV_BUF_SIZE / 2) ? WAV_BUF_SIZE / 2 : player.remainingData;

            // Przesuń dane drugiej połowy do pierwszej

            // Wczytaj nowe dane do drugiej połowy bufora
            FRESULT fr = f_read(&player.currentFile, player.wavBuf + WAV_BUF_SIZE / 2, toRead, &br);

            memcpy(player.wavBuf, player.wavBuf + WAV_BUF_SIZE / 2, WAV_BUF_SIZE / 2);

            if (br > 0) {
                player.remainingData -= br;
                player.needNewBuffer = false;
            } else {
                stop_wav();  // koniec pliku lub błąd
            }
        }

        /* Sprawdź czy odtwarzanie się zakończyło */
        if (player.isPlaying && player.remainingData == 0 && player.bufferPos >= WAV_BUF_SIZE) {
            stop_wav();
            oled_putString(1, 45, (uint8_t*)"Finished", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        }

        /* Sprawdzanie potencjometru (alternatywne sterowanie głośnością) */
        ADC_StartCmd(LPC_ADC, ADC_START_NOW);
        while (!(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)));
        uint32_t adcVal = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0);

        /* Konwersja wartości ADC (12-bit: 0-4095) na głośność (0-100) */
        uint32_t newVol = adcVal * 100 / 4095;
        if (abs(newVol - player.volume) > 5) {  // Zmiana tylko przy znaczącej różnicy
            set_volume(newVol);
        }
    }
}
