/*****************************************************************************
 *   Odtwarzacz MP3, Systemy Wbudowane 2024/2025
 *  
 *   Zadaniem algorytmu jest odczytanie plików mp3 z karty SD,
 *   oraz uruchomienie ich na głośniku.
 * 
 *   Copyright(C) 2025, Maja Binkowska, Bartosz Łężniak, Paweł Rajnert
 *   All rights reserved.
 *
 ******************************************************************************/

#include "lpc17xx_pinsel.h"		/* ustwaienie pinów */
#include "lpc17xx_gpio.h"		/* GPIO */
#include "lpc17xx_ssp.h"		/* SPI, uzywane do karty SD */
#include "lpc17xx_timer.h"		/* timer do czasu */
#include "lpc17xx_dac.h"		/* przetwornik DAC */
#include "lpc17xx_i2c.h"		/* I2C do komunikacji z wyswietlaczem OLED i linijką diodową */
#include "stdio.h"		        /* biblioteka We/Wy */
#include "lpc17xx_adc.h"

#include "diskio.h"			/* wczytana biblioteka do obslugi FAT i karty SD */
#include "ff.h"


#include "oled.h"			/* wczytana biblioteka do obslugi ekranu OLED */
#include <stdbool.h>
#include <string.h>

#define WAV_BUF_SIZE 512

/* Pinout dla LEDów (linijka diodowa) */
#define I2C_LED_EXPANDER_ADDR 0x20  // Adres expandera I2C dla linijki diodowej

/* zapisywanie danych o znalezionych plikach na karcie SD */
#define MAX_FILES 9
#define MAX_FILENAME_LEN 64

/* Struktura stanu odtwarzacza */
typedef struct {
    bool isPlaying;
    int currentTrack;
    uint32_t volume;          // Głośność 0-100
    bool screenState;         // true = visible, false = blank
    int fileCount;
    char fileList[MAX_FILES][MAX_FILENAME_LEN];
    FIL currentFile;
    HMP3Decoder hMP3Decoder;
    uint8_t mp3ReadBuffer[MP3_READ_BUFFER_SIZE] __attribute__((aligned(4)));  // Zapewnienie wyrównania 4-bajtowego
    short pcmBuffer[PCM_BUFFER_SIZE] __attribute__((aligned(4)));            // Zapewnienie wyrównania 4-bajtowego
    int bytesLeft;
    uint8_t *readPtr;
} PlayerState;

// Inicjalizacja zmiennej PlayerState z wartościami domyślnymi
PlayerState player = {
    .isPlaying = false,
    .currentTrack = 0,
    .volume = 50,
    .screenState = true,
    .fileCount = 0,
    .bytesLeft = 0,
    .readPtr = NULL,
    .hMP3Decoder = NULL  // Dodano inicjalizację na NULL
};

static FILINFO Finfo;		/* zasob przechowujacy informacje o plikach z karty SD */
static FATFS Fatfs[1];		/* implementacja fatfs do wczytywania zasobu FAT */
static uint8_t buf[64];		/* bufor pomocniczy do wyświetlania */

/* Licznik milisekund dla systemu */
static volatile uint32_t msTicks = 0;

/* Deklaracje funkcji */
static void init_ssp(void);
static void init_i2c(void);
static void init_adc(void);
static void init_dac(void);
static void init_timer(void);
static void button_init(void);
static void rotary_init(void);
static void led_bar_init(void);
static void led_bar_set(uint8_t value);
static void display_files(void);
static void play_mp3_file(const char* filename);
static void stop_mp3(void);
static void next_track(void);
static void prev_track(void);
static void set_volume(uint32_t vol);
static void delay_ms(uint32_t ms);
static uint32_t getTicks(void);
static bool init_mp3_player(void);  // Zwraca status powodzenia

/* Funkcja opóźnienia zastępująca Timer0_Wait */
static void delay_ms(uint32_t ms)
{
    uint32_t startTime = msTicks;
    while((msTicks - startTime) < ms) {
        __WFI();  // Wait for interrupt - oszczędzanie energii
    }
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
    SSP_CFG_Type SSP_ConfigStruct;		/* konfiguracja SPI */
    PINSEL_CFG_Type PinCfg;				/* konfiguracja pinow */

    /*
     * P0.7 SCK (zegar SPI)
     * P0.8 MISO (odczyt)
     * P0.9 MOSI (wyslanie danych)
     * P2.2 Ustawiony jako GPIO dla ssel (wybor chip)
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

    /* init SPI i uruchamia */
    SSP_ConfigStructInit(&SSP_ConfigStruct);
    // Zwiększenie częstotliwości zegara dla stabilniejszego działania
    SSP_ConfigStruct.ClockRate = 2000000;  // 2MHz zamiast domyślnego 1MHz
    SSP_Init(LPC_SSP1, &SSP_ConfigStruct);
    SSP_Cmd(LPC_SSP1, ENABLE);

    // Inicjalizacja pinu SSEL jako wyjścia GPIO i ustawienie go na wysoki stan
    GPIO_SetDir(2, (1<<2), 1);
    GPIO_SetValue(2, (1<<2));
}

static void init_dac(void)
{
    PINSEL_CFG_Type PinCfg;

    /* Konfiguracja pinu DAC (P0.26) */
    PinCfg.Funcnum = 2;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = 26;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);

    /* Inicjalizacja DAC - ustawienie timera BIAS */
    DAC_Init(LPC_DAC);

    /* Ustawienie początkowej wartości DAC (cisza) */
    DAC_UpdateValue(LPC_DAC, 512);
}

static void init_timer(void)
{
    TIM_TIMERCFG_Type TIM_ConfigStruct;
    TIM_MATCHCFG_Type TIM_MatchConfigStruct;

    /* Konfiguracja Timer0 - sampling rate 44.1kHz */
    TIM_ConfigStruct.PrescaleOption = TIM_PRESCALE_USVAL;
    TIM_ConfigStruct.PrescaleValue = 1;  // 1 μs

    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &TIM_ConfigStruct);

    /* Ustawienie Match Register dla 44.1kHz (22.7μs) */
    TIM_MatchConfigStruct.MatchChannel = 0;
    TIM_MatchConfigStruct.MatchValue = 23;  // ~22.7μs (44.1kHz)
    TIM_MatchConfigStruct.IntOnMatch = ENABLE;
    TIM_MatchConfigStruct.ResetOnMatch = ENABLE;
    TIM_MatchConfigStruct.StopOnMatch = DISABLE;
    TIM_MatchConfigStruct.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;

    TIM_ConfigMatch(LPC_TIM0, &TIM_MatchConfigStruct);

    /* Włączenie przerwania MR0 */
    NVIC_SetPriority(TIMER0_IRQn, 1);  // Ustaw priorytet przerwania
    NVIC_EnableIRQ(TIMER0_IRQn);

    /* Początkowo timer jest wyłączony - włączymy go przy odtwarzaniu */
    TIM_Cmd(LPC_TIM0, DISABLE);
}

/* Handler przerwania od Timer0 - obsługa odtwarzania dźwięku */
void TIMER0_IRQHandler(void)
{
    static uint32_t sample_index = 0;

    /* Sprawdź czy przerwanie pochodzi od Match Register 0 */
    if (TIM_GetIntStatus(LPC_TIM0, TIM_MR0_INT))
    {
        /* Jeśli odtwarzacz jest włączony i dane są dostępne */
        if (player.isPlaying && sample_index < PCM_BUFFER_SIZE)
        {
            /* Pobieranie próbki i skalowanie głośności */
            int16_t sample = player.pcmBuffer[sample_index];
            uint32_t scaled_sample = ((uint32_t)(sample + 32768) * player.volume) / 100;

            /* Ograniczenie wartości do zakresu DAC (10-bit: 0-1023) */
            if (scaled_sample > 1023) scaled_sample = 1023;

            /* Wysłanie próbki do DAC */
            DAC_UpdateValue(LPC_DAC, scaled_sample & 0x3FF);

            /* Przejście do następnej próbki */
            sample_index++;

            /* Jeśli przetworzono wszystkie próbki, dekoduj więcej */
            if (sample_index >= PCM_BUFFER_SIZE / 2)  // Połowa bufora (mono)
            {
                sample_index = 0;
            }
        }
        else
        {
            /* Jeśli nie odtwarza - cisza */
            DAC_UpdateValue(LPC_DAC, 512);
        }

        /* Kasowanie flagi przerwania */
        TIM_ClearIntPending(LPC_TIM0, TIM_MR0_INT);
    }
}

static void init_i2c(void)
{
    PINSEL_CFG_Type PinCfg;

    /* I2C2 pin connect */
    PinCfg.Funcnum = 2;
    PinCfg.Pinnum = 10;
    PinCfg.Portnum = 0;
    PinCfg.OpenDrain = 1;  // Używaj open drain dla I2C
    PinCfg.Pinmode = 0;    // Pull-up enabled
    PINSEL_ConfigPin(&PinCfg);

    PinCfg.Pinnum = 11;
    PinCfg.OpenDrain = 1;  // Używaj open drain dla I2C
    PINSEL_ConfigPin(&PinCfg);

    // inicjalizacja i uruchomienie
    I2C_Init(LPC_I2C2, 100000);
    I2C_Cmd(LPC_I2C2, ENABLE);

    // Dodajemy krótkie opóźnienie po inicjalizacji I2C
    delay_ms(5);
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
    delay_ms(10);

    /* Konfiguracja expandera I/O jako wyjścia dla linijki diodowej */
    config_data[0] = 0x00;  // Rejestr konfiguracji
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

    /* Dla każdego z 8 LEDów sprawdź czy powinien być zapalony */
    for (int i = 0; i < 8; i++) {
        if (value >= (i + 1) * 100 / 8) {
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
            oled_putString(10, 1 + (i * 8), (uint8_t*)player.fileList[i], OLED_COLOR_BLACK, OLED_COLOR_WHITE);
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
    /* ------ deklaracja podstawowych zmiennych ------ */
    /* zmienne potrzebne do zarzadzania systemem FAT oraz karta SD */
    DSTATUS stat;   /* status inicjalizacji karty SD */
    BYTE res;       /* zmienna pomocnicza (wynik operacji lub status funckji)*/
    DIR dir;        /* wczytany katalog z karty SD */
    int result;     /* zmienna pomocnicza dla sprintf */

    /* ------ inicjalizacja peryferiow i urzadzen ------ */
    SystemInit();   /* Inicjalizacja podstawowa systemu i zegara */

    /* konfiguracja systick, oraz przerwa 100 ms potrzebna do poprawnego skonfigurowania systemu*/
    SysTick_Config(SystemCoreClock / 1000); // 1ms ticks
    delay_ms(100);  // Krótkie opóźnienie dla stabilizacji systemów

    init_ssp();     /* SPI - komunikacja z karta SD */
    init_i2c();     /* I2C - komunikacja z OLED i linijką diodową */
    init_adc();     /* ADC - wejście analogowe (potencjometr) */
    init_dac();     /* DAC - wyjście analogowe (audio) */
    init_timer();   /* Timer - taktowanie odtwarzania dźwięku */
    button_init();  /* Inicjalizacja przycisków */
    rotary_init();  /* Inicjalizacja enkodera obrotowego */

    /* Inicjalizacja wyświetlacza OLED */
    oled_init();

    /* Inicjalizacja linijki diodowej */
    led_bar_init();

    /* ------ rozpoczecie dzialania ------ */
    /* alokacja pamieci do zapisu nazw oraz ilosc zapisanych plikow*/
    player.fileCount = 0;

    /* wyczysc ekran OLED, oraz wstaw komunikat swiadczacy o poprawnym uruchomieniu ekranu*/
    oled_clearScreen(OLED_COLOR_WHITE);
    oled_putString(1, 1, (uint8_t*)"WAV Player", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1, 9, (uint8_t*)"Inicjalizacja...", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    delay_ms(500);
    stat = disk_initialize(0);		/* inicjalizacja karty SD */

    /* sprawdzenie czy karta SD jest zainicjalizowana, oraz czy jest dostepna */
    if (stat & STA_NOINIT) {
        oled_putString(1, 18, (uint8_t*)"Blad init SD", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    if (stat & STA_NODISK) {
        oled_putString(1, 27, (uint8_t*)"Brak karty SD", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* jezeli sprawdzenie nie udalo sie, zatrzymaj program */
    if (stat != 0) {
        oled_putString(1, 36, (uint8_t*)"Blad SD, stop.", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        while(1) {
            __WFI(); // Wait for interrupt - zatrzymanie CPU w trybie oszczędzania energii
        }
    }
  
    oled_putString(1, 18, (uint8_t*)"Poprawne SD", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    /* zamontowanie systemu plikow FAT */
    res = f_mount(0, &Fatfs[0]);
    if (res != FR_OK) {
        result = sprintf((char*)buf, "Mount err: %d", res);
        oled_putString(1, 27, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        while(1) {
            __WFI();
        }
    }

    /* otwiera katalog główny karty SD */
    res = f_opendir(&dir, "/");
    if (res) {
        result = sprintf((char*)buf, "Dir err: %d", res);
        oled_putString(1, 36, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        while(1) {
            __WFI();
        }
    }

    /* ------ operacje na karcie SD ------ */
    /* zbiera, zapisuje i wyswietla pliki znalezione na karcie SD */
    player.fileCount = 0;
    for (int i = 0; i < MAX_FILES; ) {
        res = f_readdir(&dir, &Finfo);
        if ((res != FR_OK) || !Finfo.fname[0]) break;

        if (Finfo.fattrib & AM_DIR || Finfo.fname[0] == '_') continue;

        /* Sprawdź czy plik ma rozszerzenie .wav */
        char *ext = strrchr(Finfo.fname, '.');
        if (!ext || (strcasecmp(ext, ".WAV") != 0)) continue;

        /* zapisz nazwe pliku do pamieci */
        snprintf(player.fileList[player.fileCount], MAX_FILENAME_LEN, "%s", Finfo.fname);
        player.fileCount++;
        i++;
    }

    if (player.fileCount == 0) {
        oled_putString(1, 36, (uint8_t*)"Brak plikow WAV", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        delay_ms(2000);
    } else {
        result = sprintf((char*)buf, "Pliki WAV: %d", player.fileCount);
        oled_putString(1, 36, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        delay_ms(1000);
    }

    /* Wyświetl listę plików */
    display_files();
    
        // Czytanie nagłówka 44 bajty
    f_read(&file, hdr, 44, &br);
    if (br != 44 || hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F') {
        oled_putString(1,45, (uint8_t*)"Zly WAV\r\n",OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        f_close(&file);
        return 1;
    }

    sampleRate = hdr[24] | (hdr[25]<<8) | (hdr[26]<<16) | (hdr[27]<<24);
    dataSize = hdr[40] | (hdr[41]<<8) | (hdr[42]<<16) | (hdr[43]<<24);
    numChannels = hdr[22] | (hdr[23] << 8);

    delay = 1000000U / sampleRate;

    if (numChannels != 1 && numChannels != 2) {
        oled_putString(1,54, (uint8_t*)"Niewspierany kanal\r\n",OLED_COLOR_BLACK, OLED_COLOR_WHITE));
        f_close(&file);
        return 1;
    }

    // Odtwarzanie danych PCM (mono lub stereo)
    while (dataSize > 0) {
        UINT toRead = (dataSize > WAV_BUF_SIZE) ? WAV_BUF_SIZE : dataSize;
        f_read(&file, wavBuf, toRead, &br);
        if (br == 0) break;
        dataSize -= br;

        for (UINT i = 0; i + 1 < br;) {
            int16_t pcm = wavBuf[i] | (wavBuf[i+1] << 8);
            i += (numChannels == 2) ? 4 : 2; // jeśli stereo, pomiń kanał R
            uint16_t dacVal = (uint16_t)((pcm + 32768) >> 6);
            DAC_UpdateValue(LPC_DAC, dacVal);
            Timer0_us_Wait(delay);
        }
    }

    f_close(&file);
    return 0;
}

void check_failed(uint8_t *file, uint32_t line) {
    while (1);
}