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

#include "helix_mp3.h"  /* biblioteka do dekodowania MP3 */

#include "oled.h"			/* wczytana biblioteka do obslugi ekranu OLED */
#include <stdbool.h>
#include <string.h>

/* Podstawowe definicje */
#define MP3_READ_BUFFER_SIZE 4096
#define MP3_OUT_BUFFER_SIZE 1152
#define PCM_BUFFER_SIZE 2304

/* Pinout dla LEDów (linijka diodowa) */
#define I2C_LED_EXPANDER_ADDR 0x20  // Adres expandera I2C dla linijki diodowej

/* Definicje portów dla rotary enkodera */
#define ROT_A_PORT 2
#define ROT_A_PIN 10
#define ROT_B_PORT 2
#define ROT_B_PIN 11

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

/* Inicjalizacja odtwarzacza MP3 */
static bool init_mp3_player(void)
{
    /* Sprawdzenie czy wszystkie wymagane peryferia są zainicjalizowane */
    if (player.mp3ReadBuffer == NULL || player.pcmBuffer == NULL) {
        oled_putString(1, 45, (uint8_t*)"Buffer error", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return false;
    }

    /* Zapewnienie prawidłowego wyrównania pamięci dla dekodera MP3 */
    /* Dodatkowa inicjalizacja przed wywołaniem MP3InitDecoder */
    memset(player.mp3ReadBuffer, 0, MP3_READ_BUFFER_SIZE);
    memset(player.pcmBuffer, 0, PCM_BUFFER_SIZE * sizeof(short));

    /* Przygotowanie obszaru systemowego dla heap dla dekodera MP3 */
    static uint32_t mp3DecoderHeap[2048] __attribute__((aligned(8)));
    memset(mp3DecoderHeap, 0, sizeof(mp3DecoderHeap));

    /* Utworzenie dekodera MP3 */
    player.hMP3Decoder = MP3InitDecoder();

    if (player.hMP3Decoder == NULL) {
        oled_putString(1, 45, (uint8_t*)"MP3 init error", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return false;
    }

    return true;
}

/* Odtwarzanie pliku MP3 */
static void play_mp3_file(const char* filename)
{
    FRESULT res;

    /* Zatrzymaj aktualnie odtwarzany plik */
    if (player.isPlaying) {
        stop_mp3();
    }

    /* Wyczyść bufor PCM dla bezpieczeństwa */
    memset(player.pcmBuffer, 0, PCM_BUFFER_SIZE * sizeof(short));

    /* Otwórz plik MP3 */
    res = f_open(&player.currentFile, filename, FA_READ);
    if (res != FR_OK) {
        sprintf((char*)buf, "Err open: %d", res);
        oled_putString(1, 45, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return;
    }

    /* Wczytaj początkowe dane */
    UINT bytesRead;
    player.bytesLeft = 0;
    res = f_read(&player.currentFile, player.mp3ReadBuffer, MP3_READ_BUFFER_SIZE, &bytesRead);
    if (res != FR_OK || bytesRead == 0) {
        f_close(&player.currentFile);
        return;
    }

    player.bytesLeft = bytesRead;
    player.readPtr = player.mp3ReadBuffer;
    player.isPlaying = true;

    /* Włącz timer odtwarzania */
    TIM_Cmd(LPC_TIM0, ENABLE);

    /* Aktualizacja wyświetlacza */
    display_files();
}

/* Zatrzymanie odtwarzania */
static void stop_mp3(void)
{
    if (player.isPlaying) {
        TIM_Cmd(LPC_TIM0, DISABLE);
        f_close(&player.currentFile);
        player.isPlaying = false;
        player.bytesLeft = 0;
        player.readPtr = NULL;

        /* Wyczyść wyjście DAC */
        DAC_UpdateValue(LPC_DAC, 512);  // Wartość środkowa (cisza)

        /* Aktualizacja wyświetlacza */
        display_files();
    }
}

/* Następny utwór */
static void next_track(void)
{
    if (player.fileCount > 0) {
        player.currentTrack = (player.currentTrack + 1) % player.fileCount;
        if (player.isPlaying) {
            play_mp3_file(player.fileList[player.currentTrack]);
        }
        display_files();
    }
}

/* Poprzedni utwór */
static void prev_track(void)
{
    if (player.fileCount > 0) {
        player.currentTrack = (player.currentTrack - 1 + player.fileCount) % player.fileCount;
        if (player.isPlaying) {
            play_mp3_file(player.fileList[player.currentTrack]);
        }
        display_files();
    }
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

/* Dekodowanie następnej ramki MP3 */
static bool decode_next_mp3_frame(void)
{
    int offset, err;
    UINT bytesRead;

    /* Sprawdź czy odtwarzacz jest aktywny i dane są dostępne */
    if (!player.isPlaying || player.readPtr == NULL || player.hMP3Decoder == NULL) {
        return false;
    }

    /* Szukanie nagłówka MP3 */
    offset = MP3FindSyncWord(player.readPtr, player.bytesLeft);
    if (offset < 0) {
        /* Jeśli nie znaleziono nagłówka, wczytaj więcej danych */
        /* Przenieś pozostałe dane na początek bufora */
        if (player.bytesLeft > 0) {
            memmove(player.mp3ReadBuffer, player.readPtr, player.bytesLeft);
        }

        /* Wczytaj więcej danych */
        FRESULT res = f_read(&player.currentFile, player.mp3ReadBuffer + player.bytesLeft,
                            MP3_READ_BUFFER_SIZE - player.bytesLeft, &bytesRead);

        if (res != FR_OK || bytesRead == 0) {
            /* Koniec pliku lub błąd - zatrzymaj odtwarzanie lub przejdź do następnego pliku */
            next_track();  // Automatyczne przejście do następnego utworu po zakończeniu
            return false;
        }

        player.readPtr = player.mp3ReadBuffer;
        player.bytesLeft += bytesRead;

        /* Ponowna próba znalezienia nagłówka */
        offset = MP3FindSyncWord(player.readPtr, player.bytesLeft);
        if (offset < 0) {
            return false;
        }
    }

    /* Sprawdź czy offset jest w zakresie bufora */
    if (offset >= player.bytesLeft) {
        return false;
    }

    /* Przesunięcie wskaźnika do nagłówka ramki */
    player.readPtr += offset;
    player.bytesLeft -= offset;

    /* Dekodowanie ramki */
    err = MP3Decode(player.hMP3Decoder, &player.readPtr, &player.bytesLeft,
                    player.pcmBuffer, 0);

    if (err) {
        /* Błąd dekodowania - pomiń ramkę */
        if (err == ERR_MP3_INDATA_UNDERFLOW) {
            /* Brak wystarczających danych - wczytaj więcej */
            if (player.bytesLeft > 0) {
                memmove(player.mp3ReadBuffer, player.readPtr, player.bytesLeft);
            }

            FRESULT res = f_read(&player.currentFile, player.mp3ReadBuffer + player.bytesLeft,
                                MP3_READ_BUFFER_SIZE - player.bytesLeft, &bytesRead);

            if (res != FR_OK || bytesRead == 0) {
                next_track();  // Automatyczne przejście do następnego utworu po zakończeniu
                return false;
            }

            player.readPtr = player.mp3ReadBuffer;
            player.bytesLeft += bytesRead;
        }
        return false;
    }

    return true;
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
    oled_putString(1, 1, (uint8_t*)"MP3 Player", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1, 9, (uint8_t*)"Inicjalizacja...", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    delay_ms(500);
    stat = disk_initialize(0);		/* inicjalizacja karty SD */

    /* sprawdzenie czy karta SD jest zainicjalizowana, oraz czy jest dostepna */
    if (stat & STA_NOINIT) {
        oled_putString(1, 18, (uint8_t*)"Init Failed", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    if (stat & STA_NODISK) {
        oled_putString(1, 27, (uint8_t*)"No SD card", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* jezeli sprawdzenie nie udalo sie, zatrzymaj program */
    if (stat != 0) {
        oled_putString(1, 36, (uint8_t*)"SD Error. Halting.", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        while(1) {
            __WFI(); // Wait for interrupt - zatrzymanie CPU w trybie oszczędzania energii
        }
    }
  
    oled_putString(1, 18, (uint8_t*)"SD Init OK", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

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

        /* pomin foldery i ukryte pliki, akceptuj tylko pliki MP3 */
        if (Finfo.fattrib & AM_DIR || Finfo.fname[0] == '_') continue;

        /* Sprawdź czy plik ma rozszerzenie .MP3 */
        char *ext = strrchr(Finfo.fname, '.');
        if (!ext || (strcasecmp(ext, ".MP3") != 0)) continue;

        /* zapisz nazwe pliku do pamieci */
        snprintf(player.fileList[player.fileCount], MAX_FILENAME_LEN, "%s", Finfo.fname);
        player.fileCount++;
        i++;
    }

    if (player.fileCount == 0) {
        oled_putString(1, 36, (uint8_t*)"No MP3 files found", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        delay_ms(2000);
    } else {
        result = sprintf((char*)buf, "Found %d MP3 files", player.fileCount);
        oled_putString(1, 36, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        delay_ms(1000);
    }

    /* Inicjalizacja dekodera MP3 */
    if (!init_mp3_player()) {
        oled_putString(1, 45, (uint8_t*)"MP3 decoder error", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        delay_ms(2000);
    }

    /* Wyświetl listę plików */
    display_files();

    /* Ustawienie początkowej głośności */
    set_volume(player.volume);

    /* obsluga wciskania przycisku i pokrętła */
    bool lastButtonPower = true;        // Ostatni stan przycisku zasilania
    bool lastButtonPlay = true;         // Ostatni stan przycisku play/pause
    bool lastButtonNext = true;         // Ostatni stan przycisku next
    uint8_t lastRotA = 1;               // Ostatni stan pinu A enkodera
    uint8_t lastRotB = 1;               // Ostatni stan pinu B enkodera

    /* Główna pętla programu */
    while (1) {
        /* Obsługa pokrętła (sterowanie głośnością) */
        uint8_t rot_a = (GPIO_ReadValue(ROT_A_PORT) & (1 << ROT_A_PIN)) ? 1 : 0;
        uint8_t rot_b = (GPIO_ReadValue(ROT_B_PORT) & (1 << ROT_B_PIN)) ? 1 : 0;

        /* Wykrywanie zmiany stanu enkodera */
        if (rot_a != lastRotA) {
            /* Wykryto obrót - określ kierunek */
            if (rot_a != rot_b) {
                /* Obrót zgodnie z ruchem wskazówek zegara - zwiększ głośność */
                set_volume(player.volume + 5);
            } else {
                /* Obrót przeciwnie do ruchu wskazówek zegara - zmniejsz głośność */
                if (player.volume >= 5) set_volume(player.volume - 5);
                else set_volume(0);
            }
            lastRotA = rot_a;
            lastRotB = rot_b;
        }

        /* Odczyt stanu przycisków */
        bool btnPower = (GPIO_ReadValue(0) & (1 << 4)) != 0;
        bool btnPlay = (GPIO_ReadValue(0) & (1 << 5)) != 0;
        bool btnNext = (GPIO_ReadValue(0) & (1 << 6)) != 0;

        /* Wykryj wciskanie przycisku zasilania - włącz/wyłącz ekran */
        if (!btnPower && lastButtonPower) {
            player.screenState = !player.screenState;
            if (!player.screenState) {
                oled_clearScreen(OLED_COLOR_BLACK);
            } else {
                display_files();
            }
        }

        /* Wykryj wciskanie przycisku play/pause */
        if (!btnPlay && lastButtonPlay) {
            if (player.isPlaying) {
                stop_mp3();
            } else if (player.fileCount > 0) {
                play_mp3_file(player.fileList[player.currentTrack]);
            }
        }

        /* Wykryj wciskanie przycisku next */
        if (!btnNext && lastButtonNext) {
            next_track();
        }

        /* Zapamietaj stan przycisków */
        lastButtonPower = btnPower;
        lastButtonPlay = btnPlay;
        lastButtonNext = btnNext;

        /* Dekodowanie MP3 jeśli odtwarzacz jest aktywny */
        if (player.isPlaying) {
            /* Dekoduj kolejną ramkę MP3 */
            decode_next_mp3_frame();
        }

        /* Krótka pauza aby obsługa przycisków była stabilna */
        delay_ms(10);
    }
}
