/*****************************************************************************
 *   Odtwarzacz WAV, Systemy Wbudowane 2024/2025
 *
 *   Zadaniem algorytmu jest odczytanie plików WAV z karty SD,
 *   oraz uruchomienie ich na głośniku.
 *
 *   Copyright(C) 2025, Maja Binkowska, Bartosz Łężniak, Paweł Rajnert
 *   All rights reserved.
 *
 ******************************************************************************/

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_i2c.h"

#include "stdio.h"
#include "lpc17xx_adc.h"
#include <stdlib.h>

#include "diskio.h"
#include "ff.h"

#include "joystick.h"
#include "pca9532.h"

#include "oled.h"
#include <stdbool.h>
#include <string.h>

#define WAV_BUF_SIZE 512U
#define HALF_BUF_SIZE  (WAV_BUF_SIZE/2U)

#define ROT_A_PORT 2U
#define ROT_A_PIN 0U
#define ROT_B_PORT 2U
#define ROT_B_PIN 1U

/* zapisywanie danych o znalezionych plikach na karcie SD */
#define MAX_FILES 9U
#define MAX_FILENAME_LEN 64U

#define SEKUNDA 1000000U

/* Stałe dla DAC */
#define DAC_MIDDLE_VALUE 512U
#define DAC_MAX_VALUE 1023U
#define VOLUME_AMPLIFICATION_FACTOR 6U
#define VOLUME_AMPLIFICATION_DIVISOR 5U
#define PCM_MAX_POSITIVE 32767
#define PCM_MAX_NEGATIVE -32768
#define PCM_OFFSET 32768U
#define VOLUME_MAX 100U
#define UINT16_MAX_VALUE 65535U

/* Stałe dla timera */
#define TIMER_PRESCALE_VALUE 1U
#define SAMPLE_RATE_8KHZ 8000U
#define TIMER_MATCH_VALUE_8KHZ 125U

/* Stałe dla I2C */
#define I2C_RETRANSMISSIONS_MAX 3U
#define I2C_DELAY_MS 10000U

/* Stałe dla WAV */
#define WAV_HEADER_SIZE 44U
#define WAV_RIFF_SIGNATURE 'R'
#define WAV_RIFF_SIGNATURE2 'I'
#define WAV_REQUIRED_CHANNELS 1U

typedef struct {
    bool isPlaying;
    int32_t currentTrack;
    uint32_t volume;
    bool screenState;
    int32_t fileCount;
    char fileList[MAX_FILES][MAX_FILENAME_LEN];
    FIL currentFile;
    uint8_t wavBuf[WAV_BUF_SIZE];
    uint32_t sampleRate;
    uint32_t dataSize;
    uint16_t numChannels;
    uint32_t delay;
    uint32_t bufferPos;
    uint32_t remainingData;
    bool needNewBuffer;
    bool isPaused;
    uint8_t activeBuf;
    bool bufReady[2];
    uint32_t bufPos;
} PlayerState;

PlayerState player = {
    .isPlaying = false,
    .currentTrack = 0,
    .volume = 50U,
    .screenState = true,
    .fileCount = 0,
    .sampleRate = 0U,
    .dataSize = 0U,
    .numChannels = 0U,
    .delay = 0U,
    .bufferPos = 0U,
    .remainingData = 0U,
    .needNewBuffer = false,
    .isPaused = false,
    .activeBuf = 0U,
    .bufReady = {false, false},
    .bufPos = 0U
};

typedef struct {
    uint8_t lastStateA;
    uint8_t lastStateB;
    uint32_t lastDebounceTime;
    bool isInitialized;
} RotaryEncoder_t;

/* deklaracje zasobów dotyczących: enkodera, buforu wav, plików z karty SD, biblioteki Fatfs*/
static RotaryEncoder_t rotaryEncoder = {0U, 0U, 0U, false};
static uint8_t wavBuf[2][HALF_BUF_SIZE];
static FILINFO Finfo;
static FATFS Fatfs[1];

/* Licznik milisekund dla systemu */
static volatile uint32_t msTicks = 0U;

/* Deklaracje funkcji */
static void init_ssp(void);
static void init_i2c(void);
static void init_adc(void);
static void init_dac(void);
static void button_init(void);
static void rotary_init(void);
static void display_files(void);
static void play_wav_file(const char* filename);
static void stop_wav(void);
static void set_volume(uint32_t vol);
static void led_bar_set(uint8_t volume);
static uint32_t getTicks(void);
static void init_Timer(void);

/*!
 *  @brief    Zwraca aktualną wartość licznika milisekund.
 *
 *  @returns  uint32_t z aktualną wartością licznika msTicks
 *  @side effects:
 *            Brak efektów ubocznych
 */
static uint32_t getTicks(void)
{
    return msTicks;
}

/*!
 *  @brief    Obsługa przerwania systemowego.
 *
 *  @side effects:
 *            Inkrementuje globalną zmienną msTicks
 *            Wywołuje disk_timerproc() do obsługi karty SD
 */
void SysTick_Handler(void) {
    msTicks++;
    disk_timerproc();
}

/*!
 *  @brief    Inicjalizuje interfejs SPI do komunikacji z kartą SD.
 *
 *  @side effects:
 *            Konfiguruje piny P0.7, P0.8, P0.9 jako SPI
 *            Konfiguruje pin P2.2 jako GPIO dla SSEL
 *            Włącza i inicjalizuje moduł SSP1
 */
static void init_ssp(void)
{
    SSP_CFG_Type SSP_ConfigStruct;
    PINSEL_CFG_Type PinCfg;

    /*
     * Initialize SPI pin connect
     * P0.7 - SCK (zegar SPI)
     * P0.8 - MISO (dane odczytywane)
     * P0.9 - MOSI (dane wysyłane)
     * P2.2 ustawiony jako GPIO dla SSEL (chip select)
     */
    PinCfg.Funcnum = 2U;
    PinCfg.OpenDrain = 0U;
    PinCfg.Pinmode = 0U;
    PinCfg.Portnum = 0U;
    PinCfg.Pinnum = 7U;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 8U;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 9U;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Funcnum = 0U;
    PinCfg.Portnum = 2U;
    PinCfg.Pinnum = 2U;
    PINSEL_ConfigPin(&PinCfg);

    SSP_ConfigStructInit(&SSP_ConfigStruct);
    SSP_Init(LPC_SSP1, &SSP_ConfigStruct);
    SSP_Cmd(LPC_SSP1, ENABLE);
}

/*!
 *  @brief    Inicjalizuje interfejs I2C
 *
 *  @side effects:
 *            Konfiguruje piny P0.10 i P0.11 jako I2C2
 *            Inicjalizuje moduł I2C2 z częstotliwością 100 kHz
 *            Włącza moduł I2C2
 */
static void init_i2c(void) {
    PINSEL_CFG_Type PinCfg;

    /* Inicjalizacja pinów I2C2 */
    PinCfg.Funcnum = 2U;
    PinCfg.Pinnum = 10U;
    PinCfg.Portnum = 0U;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 11U;
    PINSEL_ConfigPin(&PinCfg);

    I2C_Init(LPC_I2C2, 100000U);
    I2C_Cmd(LPC_I2C2, ENABLE);
}

/*!
 *  @brief    Inicjalizuje przetwornik ADC do odczytu analogowych sygnałów.
 *
 *  @side effects:
 *            Konfiguruje pin P0.23 jako ADC0.0
 *            Inicjalizuje ADC z częstotliwością 200 kHz
 *            Włącza kanał 0 ADC bez przerwań
 */
static void init_adc(void)
{
    PINSEL_CFG_Type PinCfg;

    /*
     * Inicjalizacja ADC i laczenie pinow
     * AD0.0 na P0.23 */
    PinCfg.Funcnum = 1U;
    PinCfg.OpenDrain = 0U;
    PinCfg.Pinmode = 0U;
    PinCfg.Pinnum = 23U;
    PinCfg.Portnum = 0U;
    PINSEL_ConfigPin(&PinCfg);

    /* Konfiguracja ADC:
     * Częstotliwość 200 kHz
     * ADC kanał 0, bez przerwań
     */
    ADC_Init(LPC_ADC, 200000U);
    ADC_IntConfig(LPC_ADC, ADC_CHANNEL_0, DISABLE);
    ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_0, ENABLE);
}

/*!
 *  @brief    Inicjalizuje przetwornik DAC i wzmacniacz audio LM4811.
 *
 *  @side effects:
 *            Konfiguruje pin P0.26 jako DAC
 *            Inicjalizuje moduł DAC
 *            Konfiguruje piny sterujące wzmacniacza (P0.27, P0.28, P2.13)
 *            Aktywuje wzmacniacz przez ustawienie SHUTDN = 0
 */
static void init_dac(void) {
    PINSEL_CFG_Type PinCfg;

    /* AOUT na P0.26 jako DAC */
    PinCfg.Funcnum = 2U;
    PinCfg.OpenDrain = 0U;
    PinCfg.Pinmode = 0U;
    PinCfg.Pinnum = 26U;
    PinCfg.Portnum = 0U;
    PINSEL_ConfigPin(&PinCfg);

    /* Inicjalizacja DAC */
    DAC_Init(LPC_DAC);

    /* Konfiguracja wyjść wzmacniacza LM4811 (CLK, UP/DN, SHUTDN) */
    GPIO_SetDir(0U, 1UL << 27, 1U); /* CLK */
    GPIO_SetDir(0U, 1UL << 28, 1U); /* UP/DN */
    GPIO_SetDir(2U, 1UL << 13, 1U); /* SHUTDN */

    /* Wzmacniacz aktywny: SHUTDN = 0 */
    GPIO_ClearValue(0U, 1UL << 27);
    GPIO_ClearValue(0U, 1UL << 28);
    GPIO_ClearValue(2U, 1UL << 13);
}

/*!
 *  @brief    Inicjalizuje przyciski sterujące odtwarzaczem.
 *
 *  @side effects:
 *            Konfiguruje piny P0.4, P0.5, P0.6 jako GPIO wejściowe
 *            P0.4 - główny przycisk zasilania
 *            P0.5 - przycisk play/pause
 *            P0.6 - przycisk next
 */
static void button_init(void)
{
    /* P0.4 jako wejscie GPIO - główny przycisk zasilania */
    PINSEL_CFG_Type PinCfg;
    PinCfg.Funcnum = 0U;
    PinCfg.OpenDrain = 0U;
    PinCfg.Pinmode = 0U;
    PinCfg.Pinnum = 4U;
    PinCfg.Portnum = 0U;
    PINSEL_ConfigPin(&PinCfg);

    /* ustaw P0.4 jako input */
    GPIO_SetDir(0U, 1U << 4, 0U);
}

/*!
 *  @brief    Handler przerwania Timer1 - generuje próbki audio w częstotliwości 8kHz.
 *
 *  @side effects:
 *            Odczytuje próbki PCM z podwójnego bufora
 *            Przetwarza sygnał przez wzmocnienie i kontrolę głośności
 *            Wysyła wynik do DAC
 *            Przełącza między buforami gdy jeden się wyczerpie
 *            Kasuje flagę przerwania Timer1
 */
void TIMER1_IRQHandler(void) {
    if (!TIM_GetIntStatus(LPC_TIM1, TIM_MR0_INT)) return;
    if (player.isPlaying && !player.isPaused && player.bufReady[player.activeBuf]) {

		/* Odczyt próbki z bufora */
        uint8_t *buf = wavBuf[player.activeBuf];
        int16_t samp = buf[player.bufPos] | (buf[player.bufPos+1] << 8);

		/* Podgłośnienie i skalowanie */
        int32_t amp = samp * 6/5;
        amp = (amp>32767?32767:(amp<-32768?-32768:amp));
        uint32_t u = (amp + 32768) * player.volume / 100;
        uint16_t dac = u * 1023 / 65535;
        DAC_UpdateValue(LPC_DAC, dac);
        player.bufPos += 2;

        if (player.bufPos >= HALF_BUF_SIZE) {
            player.bufReady[player.activeBuf] = false;
            player.activeBuf ^= 1;
            player.bufPos = 0;
        }
    }
    TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
}

/*!
 *  @brief    Otwiera i odtwarza plik WAV o podanej nazwie.
 *  @param filename
 *            Nazwa pliku WAV do odtworzenia
 *
 *  @side effects:
 *            Zatrzymuje aktualnie odtwarzany plik
 *            Otwiera nowy plik i sprawdza nagłówek WAV
 *            Wypełnia bufory danymi audio
 *            Uruchamia timer dla próbkowania 8kHz
 *            Wyświetla status na ekranie OLED
 */
static void play_wav_file(const char* filename) {
    FRESULT fr;
    UINT    br;
    uint8_t hdr[WAV_HEADER_SIZE];

    stop_wav();
    fr = f_open(&player.currentFile, filename, FA_READ);
    if (fr != FR_OK) {
        oled_putString(1U, 45U, (uint8_t*)"Open err", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return;
    }

    f_read(&player.currentFile, hdr, WAV_HEADER_SIZE, &br);

	/* Sprawdzenie nagłówka WAV */
    player.sampleRate = (uint32_t)(hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24));
    player.remainingData = (uint32_t)(hdr[40] | (hdr[41] << 8) | (hdr[42] << 16) | (hdr[43] << 24));
    player.numChannels = (uint16_t)(hdr[22] | (hdr[23] << 8));

    if ((hdr[0] != WAV_RIFF_SIGNATURE) || (hdr[1] != WAV_RIFF_SIGNATURE2) ||
        (player.numChannels != WAV_REQUIRED_CHANNELS) || (player.sampleRate != SAMPLE_RATE_8KHZ)) {
        oled_putString(1U, 45U, (uint8_t*)"Fmt err", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        f_close(&player.currentFile);
        return;
    }

	/* Inicjalizacja stanu odtwarzacza */
    player.isPlaying = true;
    player.isPaused = false;
    player.activeBuf = 0U;
    player.bufPos = 0U;

	/* Wstępne wypełnienie obu połówek bufora */
    for (int32_t i = 0; i < 2; i++) {
        UINT toRead = (player.remainingData > HALF_BUF_SIZE) ? HALF_BUF_SIZE : player.remainingData;
        fr = f_read(&player.currentFile, wavBuf[i], toRead, &br);
        player.bufReady[i] = (br > 0U);
        player.remainingData -= br;
    }

	/* Start timera dla próbkowania 8 kHz */
    TIM_Cmd(LPC_TIM1, ENABLE);
    oled_putString(1U, 45U, (uint8_t*)"PLAYING...", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
}

/*!
 *  @brief    Zatrzymuje odtwarzanie pliku WAV.
 *
 *  @side effects:
 *            Wyłącza timer próbkowania
 *            Zamyka aktualnie otwarty plik
 *            Ustawia DAC na wartość środkową (cisza)
 *            Resetuje flagi odtwarzania
 */
static void stop_wav(void) {
    if (player.isPlaying) {
        player.isPlaying = false;
        player.isPaused = false;
        TIM_Cmd(LPC_TIM1, DISABLE);
        f_close(&player.currentFile);
        DAC_UpdateValue(LPC_DAC, DAC_MIDDLE_VALUE);
    }
}

/*!
 *  @brief    Inicjalizuje Timer1 dla generowania próbek audio w częstotliwości 8 kHz.
 *
 *  @side effects:
 *            Konfiguruje Timer1 z prescalerem 1 mikrosekundy
 *            Ustawia match value na 125 mikrosekund (8kHz)
 *            Włącza przerwania Timer1
 *            Uruchamia timer
 */
static void init_Timer(void)
{
    TIM_TIMERCFG_Type Config;
    TIM_MATCHCFG_Type Match_Cfg;

    Config.PrescaleOption = TIM_PRESCALE_USVAL;
    Config.PrescaleValue = TIMER_PRESCALE_VALUE; /* 1 mikrosekunda */

    /* Wyłącz timer przed konfiguracją */
    TIM_Cmd(LPC_TIM1, DISABLE);

    /* Inicjalizuj timer */
    TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &Config);

    /* Konfiguracja 'Match' dla 8kHz */
    Match_Cfg.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
    Match_Cfg.IntOnMatch = TRUE;
    Match_Cfg.ResetOnMatch = TRUE;
    Match_Cfg.StopOnMatch = FALSE;
    Match_Cfg.MatchChannel = 0U;

    /* Dla 8kHz: 1000000/8000 = 125 mikrosekund */
    Match_Cfg.MatchValue = TIMER_MATCH_VALUE_8KHZ;

    TIM_ConfigMatch(LPC_TIM1, &Match_Cfg);

    /* Włącz przerwania */
    NVIC_EnableIRQ(TIMER1_IRQn);

    /* Włącz timer */
    TIM_Cmd(LPC_TIM1, ENABLE);
}

/*!
 *  @brief    Inicjalizuje enkoder obrotowy (rotary encoder).
 *
 *  @side effects:
 *            Konfiguruje piny P2.0 i P2.1 jako GPIO wejściowe
 *            P2.0 - ROT_A (sygnał A enkodera)
 *            P2.1 - ROT_B (sygnał B enkodera)
 */
static void rotary_init(void)
{
    PINSEL_CFG_Type PinCfg;
    PinCfg.Funcnum = 0U;
    PinCfg.OpenDrain = 0U;
    PinCfg.Pinmode = 0U;

    /* ROT_A */
    PinCfg.Portnum = ROT_A_PORT;
    PinCfg.Pinnum = ROT_A_PIN;
    PINSEL_ConfigPin(&PinCfg);

    /* ROT_B */
    PinCfg.Portnum = ROT_B_PORT;
    PinCfg.Pinnum = ROT_B_PIN;
    PINSEL_ConfigPin(&PinCfg);

	/* Ustawienie jako wejście */
    GPIO_SetDir(ROT_A_PORT, (1U << ROT_A_PIN), 0U);
    GPIO_SetDir(ROT_B_PORT, (1U << ROT_B_PIN), 0U);
}

/*!
 *  @brief    Ustawia linijkę LED zgodnie z poziomem głośności
 *  @param volume
 *            Poziom głośności w zakresie 0-100
 *
 *  @side effects:
 *            Aktualizuje stan diód LED na linijce zgodnie z głośnością
 *            0-1%: wszystkie LED wyłączone
 *            99-100%: wszystkie LED włączone
 *            Pomiędzy: proporcjonalnie włącza LED od lewej do prawej
 */
static void led_bar_set(uint8_t volume) {
    uint16_t ledOn = 0;
    uint16_t ledOff = 0xFFFF;  // Wszystkie LED domyślnie wyłączone
    uint8_t ledsToLight = 0;
    int i;

    // 0%: wszystkie LED wyłączone
    if (volume == 0) {
        ledOn = 0;
        ledOff = 0xFFFF;
    }
    // 100%: wszystkie LED włączone
    else if (volume >= 100) {
        ledOn = 0x7FFF;
        ledOff = 0;
    }
    // 1-99%: proporcjonalnie włącz LED
    else {
        ledsToLight = (volume * 8) / 100;

        // Włącz odpowiednią liczbę LED od lewej strony
        for (i = 0; i < ledsToLight && i < 8; i++) {
            ledOn |= (1 << i);
        }

        // Wyłącz pozostałe LED
        for (i = ledsToLight; i < 8; i++) {
            ledOff |= (1 << i);
        }
    }

    // Ustaw stan LED
    pca9532_setLeds(ledOn, ledOff);
}


/*!
 *  @brief    Wyświetla listę plików WAV na ekranie OLED z oznaczeniem aktualnie wybranego utworu.
 * 
 *  @side effects:
 *            Czyści ekran OLED i wyświetla listę plików z player.fileList.
 *            Wybrany utwór jest oznaczony symbolem ">" i odwróconymi kolorami.
 *            Jeśli ekran jest wyłączony (player.screenState == false), funkcja kończy działanie wcześniej.
 */
static void display_files(void)
{
    int32_t i;

    /* Sprawdź czy ekran jest włączony */
    if (player.screenState == false) {
        return;
    }

	/* Wyczyść ekran */
    oled_clearScreen(OLED_COLOR_WHITE);

    /* Oznaczenie aktualnie wybranego pliku */
    for (i = 0; i < player.fileCount; i++) {
        if (i == player.currentTrack) {
            /* Wyróżnienie aktualnego utworu */
            oled_putString(1, 1 + (i * 8), (uint8_t*)"> ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            oled_putString(10, 1 + (i * 8), (uint8_t*)player.fileList[i], OLED_COLOR_WHITE, OLED_COLOR_BLACK);
        }
        else {
            oled_putString(1, 1 + (i * 8), (uint8_t*)"  ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            oled_putString(10, 1 + (i * 8), (uint8_t*)player.fileList[i], OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        }
    }
}

/*!
 *  @brief    Ustawia poziom głośności i aktualizuje linijkę diodową.
 *  @param vol
 *            Poziom głośności w zakresie 0-100. Wartości powyżej 100 są ograniczane do 100.
 * 
 *  @side effects:
 *            Modyfikuje player.volume.
 */
static void set_volume(uint32_t vol)
{
    uint32_t limited_vol = vol;

	/* Ograniczenie głośności do zakresu 0-100 */
    if (limited_vol > VOLUME_MAX) {
        limited_vol = VOLUME_MAX;
    }

    player.volume = limited_vol;
    led_bar_set((uint8_t)player.volume);
}

/*!
 *  @brief    Główna funkcja programu - inicjalizuje system i obsługuje pętlę główną odtwarzacza WAV.
 *  @returns  Kod zakończenia programu: 1 w przypadku błędu, 0 w przypadku normalnego zakończenia
 * 
 *  @side effects:
 *            Inicjalizuje wszystkie peryferia.
 *            Montuje, weryfikuje kartę SD i skanuje pliki WAV.
 *            Uruchamia pętlę główną obsługującą odtwarzanie muzyki i interfejs użytkownika.
 *            Wyświetla informacje na ekranie OLED.
 */
int main(void) {
    DSTATUS stat;
    FRESULT fr;
    DIR dir;
    uint32_t lastADCCheck = 0U;
    bool lastButtonPower = true;
    bool screenNeedsUpdate = false;
    char msg[32];
    char* ext;
    uint32_t now;
    bool btnPower;
    uint32_t v;
    int32_t volume_diff;
    int32_t i;
    UINT br;
    UINT toRead;

    SystemInit();
	/* Inicjalizacja zegara systemowego 1 ms*/
    SysTick_Config(SystemCoreClock / 1000U);

    /* krótkie opóźnienie dla stabilizacji systemu */
    Timer0_us_Wait(100000U);

    init_ssp();
    init_i2c();
    init_adc();
    button_init();
    rotary_init();
    init_dac();
    init_Timer();
	pca9532_init();
    joystick_init();
    Timer0_us_Wait(SEKUNDA);

    oled_init();
    oled_clearScreen(OLED_COLOR_WHITE);
    oled_putString(1, 1, (uint8_t*)"WAV Player", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1, 10, (uint8_t*)"Init...", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    /* Ustawienie początkowego poziomu głośności */
    set_volume(50U);
    led_bar_set((uint8_t)player.volume);

	/* Inicjalizacja karty SD */
    stat = disk_initialize(0);

	/* Sprawdzenie statusu karty SD */
    if ((stat & STA_NOINIT) != 0U) {
        oled_putString(1, 19, (uint8_t*)"nie init.", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    if ((stat & STA_NODISK) != 0U) {
        oled_putString(1, 28, (uint8_t*)"brak karty", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    oled_clearScreen(OLED_COLOR_WHITE);

    /* Montowanie karty SD i inicjalizacja FAT */
    fr = f_mount(0, &Fatfs[0]);
    if (fr != FR_OK) {
        oled_putString(1, 20, (uint8_t*)"err. mont. SD", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return 1;
    }
    else {
        oled_putString(1, 20, (uint8_t*)"SD OK", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* Otwarcie katalogu głównego */
    fr = f_opendir(&dir, "/");
    if (fr != FR_OK) {
        oled_putString(1, 30, (uint8_t*)"err. otw. dir", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return 1;
    }

    /* Skanowanie plików WAV */
    player.fileCount = 0;
    while (player.fileCount < MAX_FILES) {
        /* Odczytanie kolejnego wpisu w katalogu */
        fr = f_readdir(&dir, &Finfo);
        if ((fr != FR_OK) || (Finfo.fname[0] == 0)) {
            break;
        }

        /* Sprawdzenie czy to plik WAV */
        if ((Finfo.fattrib & AM_DIR) == 0U) {
            ext = strrchr(Finfo.fname, '.');
            if ((ext != NULL) && ((strcmp(ext, ".WAV") == 0) || (strcmp(ext, ".wav") == 0))) {
                /* Zapisanie nazwy pliku na liście */
                (void)strncpy(player.fileList[player.fileCount], Finfo.fname, MAX_FILENAME_LEN - 1);
                player.fileList[player.fileCount][MAX_FILENAME_LEN - 1] = '\0';
                player.fileCount++;
            }
        }
    }

    /* Wyświetlanie informacji o liczbie znalezionych plików */
    (void)sprintf(msg, "Znaleziono %d pliki", player.fileCount);
    oled_putString(1, 30, (uint8_t*)msg, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    Timer0_us_Wait(100000U);
    display_files();
    if (player.fileCount > 0) {
        play_wav_file(player.fileList[player.currentTrack]);
    }

	/* Główna pętla programu */
    while (1) {
        now = getTicks();

        /* głośność ADC */
        if ((now - lastADCCheck) >= 200U) {
            lastADCCheck = now;
            ADC_StartCmd(LPC_ADC, ADC_START_NOW);
            while (ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE) == 0U) {
                /* Oczekiwanie na zakończenie konwersji */
            }
            v = (ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0) * 100U) / 4095U;
            volume_diff = (int32_t)v - (int32_t)player.volume;
            if ((volume_diff > 2) || (volume_diff < -2)) {
                set_volume(v);
            }
        }

        /* podwójny bufor */
        if ((player.isPlaying == true) && (player.isPaused == false)) {
            for (i = 0; i < 2; i++) {
                if ((player.bufReady[i] == false) && (player.remainingData > 0U)) {
                    toRead = (player.remainingData > HALF_BUF_SIZE) ? HALF_BUF_SIZE : player.remainingData;
                    fr = f_read(&player.currentFile, wavBuf[i], toRead, &br);
                    if ((fr == FR_OK) && (br > 0U)) {
                        player.remainingData -= br;
                        player.bufReady[i] = true;
                    }
                    else {
                        stop_wav();
                    }
                }
            }
        }

        /* włączenie i wyłączenie odtwarzacza */
        btnPower = (GPIO_ReadValue(0) & (1UL << 4)) != 0U;
        if ((btnPower == false) && (lastButtonPower == true)) {
            player.screenState = !player.screenState;
            screenNeedsUpdate = true;
        }

        if (screenNeedsUpdate == true) {
            if (player.screenState == false) {
                oled_clearScreen(OLED_COLOR_BLACK);
                stop_wav();
            }
            else {
                display_files();
                if (player.fileCount > 0) {
                    play_wav_file(player.fileList[player.currentTrack]);
                }
            }
            screenNeedsUpdate = false;
        }
        lastButtonPower = btnPower;

        /* zakończenie odtwarzania dźwięku */
        if ((player.isPlaying == true) && (player.isPaused == false) && (player.remainingData == 0U)
            && (player.bufReady[0] == false) && (player.bufReady[1] == false)) {
            stop_wav();
            oled_putString(1, 45, (uint8_t*)"Zakonczono", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        }
    }
}
