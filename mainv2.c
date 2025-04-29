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
#include "stdio.h"		        /* biblioteka We/Wy */
#include "lpc17xx_adc.h"

#include "diskio.h"			/* wczytana biblioteka do obslugi FAT i karty SD */
#include "ff.h"

#include "oled.h"			/* wczytana biblioteka do obslugi ekranu OLED */
#include <stdbool.h>

static FILINFO Finfo;		/* zasob przechowujacy informacje o plikach z karty SD */
static FATFS Fatfs[1];		/* implementacja fatfs do wczytywania zasobu FAT */
static uint8_t buf[64];		/* obecnie nieuzywany bufor UART */

/* zapisywanie danych o znalezionych plikach na karcie SD */
#define MAX_FILES 9
#define MAX_FILENAME_LEN 64

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
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);
	SSP_Cmd(LPC_SSP1, ENABLE);

}

void SysTick_Handler(void) {
	/* obsluga przerwan */
    disk_timerproc();
}


static uint32_t msTicks = 0;


static uint32_t getTicks(void)
{
	/* pobiera wartosc przerwania systemowego */
    return msTicks;
}


static void init_i2c(void)
{
	PINSEL_CFG_Type PinCfg;

	/* I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// inicjalizacja i uruchomienie
	I2C_Init(LPC_I2C2, 100000);
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

static void button_init(void)
{
    /* P0.4 jako wejscie GPIO */
    PINSEL_CFG_Type PinCfg;
    PinCfg.Funcnum = 0;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0; // pullup
    PinCfg.Pinnum = 4;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);

    /* ustaw P0.4 jako input */
    GPIO_SetDir(0, 1 << 4, 0);
}

#define ROT_A_PORT 2
#define ROT_A_PIN 10
#define ROT_B_PORT 2
#define ROT_B_PIN 11

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


int main (void) {
    /* ------ deklaracja podstawowych zmiennych ------ */
    /* zmienne potrzebne do zarzadzania systemem FAT oraz karta SD */
    DSTATUS stat;   /* status inicjalizacji karty SD */
    DWORD p2;       /* liczba sektorow */
    WORD w1;        /* rozmiar jednego sektora */
    BYTE res, b1;   /* zmienne pomocnicze (wynik operacji lub status funckji)*/
    DIR dir;        /* wczytany katalog z karty SD */

    /* ------ inicjalizacja peryferiow i urzadzen ------ */
    init_ssp();     /* SPI - komunikacja z karta SD */
    init_i2c();     /* I2C - komunikacja z OLEDem */
    init_adc();     /* ADC - konfiguracja wejsc analogowych */

    /* ------ inicjalizacja modulow wyswietlacza OLED i przycisku ------ */
    oled_init();    /* wyswietlacz OLED */
    button_init();  /* przycisk - pozwala na wlaczenie/wylaczenie ekranu OLED */
    rotary_init();

    /* ------ rozpoczecie dzialania ------ */
    /* iterator potrzebny do operacji */
    int i = 0;

    /* alokacja pamieci do zapisu nazw oraz ilosc zapisanych plikow*/
    char fileList[MAX_FILES][MAX_FILENAME_LEN];
    int fileCount = 0;

    /* uwaga: działająca karta SD to niebieska 128, sformatowana do FAT, najniższy rozmiar alokacji bloku */
    
    /* wyczysc ekran OLED, oraz wstaw komunikat swiadczacy o poprawnym uruchomieniu ekranu*/
    oled_clearScreen(OLED_COLOR_WHITE);
    oled_putString(1, 1, (uint8_t*)"OLED ON", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    /* konfiguracja systick, oraz przerwa 500 ms potrzebna do poprawnego skonfigurowania systemu*/
    SysTick_Config(SystemCoreClock / 100);
    Timer0_Wait(500);
    stat = disk_initialize(0);		/* inicjalizacja karty SD */

	/* sprawdzenie czy karta SD jest zainicjalizowana, oraz czy jest dostepna */
    if (stat & STA_NOINIT) {
        oled_putString(1, 9, (uint8_t*)"Init Failed", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    if (stat & STA_NODISK) {
        oled_putString(1, 18, (uint8_t*)"No SD fail", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* jezeli sprawdzenie nie udalo sie, zatrzymaj program */
    if (stat != 0) {
        return 1;
    }
  
    oled_putString(1, 9, (uint8_t*)"Init OK", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    /* ------ wyswietlenie danych dot. wykrytej karty SD ------ */
    /* wyswietla liczbe sektorow */
    if (disk_ioctl(0, GET_SECTOR_COUNT, &p2) == RES_OK) {
        i = sprintf((char*)buf, "ilo sek: %d \r\n", p2);
        oled_putString(1, 18, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* wyswietla rozmiar pojedynczego sektora */
    if (disk_ioctl(0, GET_SECTOR_SIZE, &w1) == RES_OK) {
        i = sprintf((char*)buf, "rozm sek: %d \r\n", w1);
        oled_putString(1, 27, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* wyswietla wielkosc bloku, czyli liczbe sektorow w jednym bloku */
    if (disk_ioctl(0, GET_BLOCK_SIZE, &p2) == RES_OK) {
        i = sprintf((char*)buf, "rozm blok: %d \r\n", p2);
        oled_putString(1, 36, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* wyswietla typ karty zdefiniowana przez sterownik */
    if (disk_ioctl(0, MMC_GET_TYPE, &b1) == RES_OK) {
        i = sprintf((char*)buf, "SD typ: %d \r\n", b1);
        oled_putString(1, 45, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* zamontowanie systemu plikow FAT */
    res = f_mount(0, &Fatfs[0]);
    if (res != FR_OK) {
        i = sprintf((char*)buf, "montow. err: %d \r\n", res);
        oled_putString(1, 54, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return 1;
    }

    /* otwiera katalog główny karty SD */
    res = f_opendir(&dir, "/");
    if (res) {
        i = sprintf((char*)buf, "otw. err /: %d \r\n", res);
        oled_putString(1, 63, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return 1;
    }

    /* ------ operacje na karcie SD ------ */
    /* zbiera, zapisuje i wyswietla pliki znalezione na karcie SD */
    fileCount = 0;
    for (int i = 0; i < MAX_FILES; ) {
        res = f_readdir(&dir, &Finfo);
        if ((res != FR_OK) || !Finfo.fname[0]) break;

        /* pomin foldery i ukryte pliki */
        if (Finfo.fattrib & AM_DIR || Finfo.fname[0] == '_') continue;

        /* zapisz nazwe pliku do pamieci */
        snprintf(fileList[fileCount], MAX_FILENAME_LEN, "%s", Finfo.fname);
        fileCount++;
        i++;
    }

    /* wyswietl wszystkie wczytane pliki */
    oled_clearScreen(OLED_COLOR_WHITE);
    for (int i = 0; i < fileCount; i++) {
        oled_putString(1, 1 + (i * 8), (uint8_t*)fileList[i], OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }


    /* obsluga wciskania przycisku */
    bool screenState = true;       // true = visible, false = blank
    bool lastButton = true;        // last button state
    bool screenNeedsUpdate = false; // marks whether screen should be redrawn

    // --- Place this before the while loop ---
    // Ustawiamy wartość początkową low, co zapobiega "akcji startowej"
    bool lastRotState = false;
    bool rotaryScreenState = true;  // Start z ekranem w stanie "włączonym" (białym)
    int rotCounter = 0;             // Licznik impulsów


    // --- Inside while(1) ---
    while (1)
    {
    	// Read rotary pins
    	bool rot_a = (GPIO_ReadValue(ROT_A_PORT) & (1 << ROT_A_PIN)) != 0;
    	bool rot_b = (GPIO_ReadValue(ROT_B_PORT) & (1 << ROT_B_PIN)) != 0;

    	// Używamy XOR do wykrycia impulsu
    	bool currentRotState = rot_a ^ rot_b;

    	// Wykryj zbocze narastające (przejście ze stanu 0 -> 1)
    	if (currentRotState && !lastRotState)
    	{
    	    rotCounter++;

    	    if (rotCounter == 1)
    	    {
    	        // Po pierwszym impulsie: ustaw ekran na czarny
    	        rotaryScreenState = false;
    	        oled_clearScreen(OLED_COLOR_BLACK);
    	    }
    	    else if (rotCounter == 2)
    	    {
    	        // Po drugim impulsie: ustaw ekran na biały oraz wyświetl listę plików
    	        rotaryScreenState = true;
    	        oled_clearScreen(OLED_COLOR_WHITE);
    	        for (int i = 0; i < fileCount; i++) {
    	            oled_putString(1, 1 + (i * 8),
    	                           (uint8_t*)fileList[i],
    	                           OLED_COLOR_BLACK,
    	                           OLED_COLOR_WHITE);
    	        }
    	        // Reset licznika, aby cykl mógł się powtarzać
    	        rotCounter = 0;
    	    }
    	}

    	// Aktualizacja poprzedniego stanu
    	lastRotState = currentRotState;


        /* odczytaj wartosc na P0.4 */
        bool btn = (GPIO_ReadValue(0) & (1 << 4)) != 0;

        /* wykryj opadanie (wciskanie przycisku) */
        if (!btn && lastButton)
        {
            screenState = !screenState;
            screenNeedsUpdate = true;
        }
        /* wykonaj akcje tylko raz, przy zmianie stanu ekranu */
        if (screenNeedsUpdate)
        {
            if (!screenState) {
                oled_clearScreen(OLED_COLOR_BLACK);
            } else {
                oled_clearScreen(OLED_COLOR_WHITE);
                for (int i = 0; i < fileCount; i++) {
                    oled_putString(1, 1 + (i * 8), (uint8_t*)fileList[i], OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                }
            }

            /* reset flagi */
            screenNeedsUpdate = false;
        }

        lastButton = btn;
}
}
