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
#include "lpc17xx_uart.h"		/* UART */
#include "lpc17xx_timer.h"		/* timer do czasu */
#include "stdio.h"		        /* biblioteka We/Wy */
#include "lpc17xx_adc.h"

#include "diskio.h"			/* wczytana biblioteka do obslugi FAT i karty SD */
#include "ff.h"

#include "oled.h"			/* wczytana biblioteka do obslugi ekranu OLED */
#include "acc.h"

#define UART_DEV LPC_UART3		/* definicja domyslnego UART */

static volatile uint8_t oledEnabled = 1;    /* flaga stanu OLED (On/Off)*/

static FILINFO Finfo;		/* zasob przechowujacy informacje o plikach z karty SD */
static FATFS Fatfs[1];		/* implementacja fatfs do wczytywania zasobu FAT */
static uint8_t buf[64];		/* obecnie nieuzywany bufor UART */

static void init_uart(void)		/* init UART */
{
	PINSEL_CFG_Type PinCfg;
	UART_CFG_Type uartCfg;

	/* Initialize UART3 pin connect */		/* konfiguruje UART3 na P0.0 i P0.1 */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 1;
	PINSEL_ConfigPin(&PinCfg);

	uartCfg.Baud_rate = 115200;		/* ustawia prędkość transmisji */
	uartCfg.Databits = UART_DATABIT_8;		/* ustawia 8 bitów danych */
	uartCfg.Parity = UART_PARITY_NONE;		/* ustawia brak parzystości */
	uartCfg.Stopbits = UART_STOPBIT_1;		/* ustawia 1 bit stopu */

	UART_Init(UART_DEV, &uartCfg);		/* inicjalizuje UART */

	UART_TxCmd(UART_DEV, ENABLE);		/* włącza transmisję */

}


static void init_ssp(void)		/* inicjalizacja interfejsu SPI */
{
	SSP_CFG_Type SSP_ConfigStruct;		/* deklaracja struktury konfiguracyjej dla SPI */
	PINSEL_CFG_Type PinCfg;				/* i pinów */

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK (zegar SPI)
	 * P0.8 - MISO (dane odczytywane)
	 * P0.9 - MOSI (dane wysyłane)
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

	SSP_ConfigStructInit(&SSP_ConfigStruct);		/* inicjalizuje SPI i włącza go (kod do końca) */

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}

void SysTick_Handler(void) {		/* obsługa przerwania SysTick */
    disk_timerproc();
}


// inicjalizacja oled_periph

static uint32_t msTicks = 0;


static uint32_t getTicks(void)
{
    return msTicks;
}


static void init_i2c(void)
{
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
	 * Init ADC pin connect
	 * AD0.0 on P0.23
	 */
	PinCfg.Funcnum = 1;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Pinnum = 23;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);

	/* Configuration for ADC :
	 * 	Frequency at 0.2Mhz
	 *  ADC channel 0, no Interrupt
	 */
	ADC_Init(LPC_ADC, 200000);
	ADC_IntConfig(LPC_ADC,ADC_CHANNEL_0,DISABLE);
	ADC_ChannelCmd(LPC_ADC,ADC_CHANNEL_0,ENABLE);

}
/* zobaczymy potem
void button_init(void) {
    PINSEL_CFG_Type PinCfg;

    // Konfiguracja pinu P0.4 jako GPIO:
    // - Port 0, Pin 4
    // - Funkcja 0 - czyli GPIO (domyślna)
    // - Pinmode ustawiony na 0, co zwykle oznacza aktywację rezystora pull-up
    // - OpenDrain wyłączony (0) - używamy trybu push-pull
    PinCfg.Portnum   = 0;
    PinCfg.Pinnum    = 4;
    PinCfg.Funcnum   = 0;       // wybieramy funkcję GPIO
    PinCfg.Pinmode   = 0;       // pull-up włączony (możesz zmienić w zależności od sprzętu)
    PinCfg.OpenDrain = 0;
    PINSEL_ConfigPin(&PinCfg);

    // Ustawienie kierunku pinu jako wejście
    // Trzeci argument równy 0 oznacza konfigurację jako wejście
    GPIO_SetDir(0, (1 << 4), 0);
}
*/

int main (void) {
    /* ------ deklaracja podstawowych zmiennych ------ */
    /* zmienne potrzebne do zarzadzania systemem FAT oraz karta SD */
    DSTATUS stat;   /* status inicjalizacji karty SD */
    DWORD p2;       /* liczba sektorow */
    WORD w1;        /* rozmiar jednego sektora */
    BYTE res, b1;   /* zmienne pomocnicze (wynik operacji lub status funckji)*/
    DIR dir;        /* wczytany katalog z karty SD */

    /* ------ zmienne czujnikow i korekcji ------ */
    /* ustawienia do obslugi akcelerometru oraz korekty odczytow */
    int32_t xoff = 0;
    int32_t yoff = 0;
    int32_t zoff = 0;

    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;

    int32_t t = 0;
    uint32_t lux = 0;
    uint32_t trim = 0;

    /* ------ inicjalizacja peryferiow i urzadzen ------ */
    init_ssp();     /* SPI - komunikacja z karta SD */
    //init_uart(); nieuzywana inicjalizacja UART'u

    init_i2c();     /* I2C - komunikacja z OLEDem */
    init_adc();     /* ADC - konfiguracja wejsc analogowych */

    /* ------ inicjalizacja modulow wyswietlacza OLED i czujnikow ------ */
    oled_init();    /* wyswietlacz OLED */
    light_init();   /* czujnik swiatla (do kontroli natezenia) */
    acc_init();     /* akcelerometr (przyspieszenia) */
    button_init();  /* przycisk - pozwala na wlaczenie/wylaczenie ekranu OLED */

    /* ------ rozpoczecie dzialania ------ */
    /* iterator potrzebny do operacji */
    int i = 0;

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
  
    oled_putString(1, 9, (uint8_t*)"Inicjalizacja OK", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    /* ------ wyswietlenie danych dot. wykrytej karty SD ------ */
    /* wyswietla liczbe sektorow */
    if (disk_ioctl(0, GET_SECTOR_COUNT, &p2) == RES_OK) {
        i = sprintf((char*)buf, "Sektorow: %d \r\n", p2);
        oled_putString(1, 18, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* wyswietla rozmiar pojedynczego sektora */
    if (disk_ioctl(0, GET_SECTOR_SIZE, &w1) == RES_OK) {
        i = sprintf((char*)buf, "Rozmiar sektora: %d \r\n", w1);
        oled_putString(1, 27, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* wyswietla wielkosc bloku, czyli liczbe sektorow w jednym bloku */
    if (disk_ioctl(0, GET_BLOCK_SIZE, &p2) == RES_OK) {
        i = sprintf((char*)buf, "Wielkosc bloku: %d \r\n", p2);
        oled_putString(1, 36, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* wyswietla typ karty zdefiniowana przez sterownik */
    if (disk_ioctl(0, MMC_GET_TYPE, &b1) == RES_OK) {
        i = sprintf((char*)buf, "Typ SD: %d \r\n", b1);
        oled_putString(1, 45, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    }

    /* zamontowanie systemu plikow FAT */
    res = f_mount(0, &Fatfs[0]);
    if (res != FR_OK) {
        i = sprintf((char*)buf, "Blad montowania: %d \r\n", res);
        oled_putString(1, 54, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return 1;
    }

    /* otwiera katalog główny karty SD */
    res = f_opendir(&dir, "/");
    if (res) {
        i = sprintf((char*)buf, "Blad otwierania /: %d \r\n", res);
        oled_putString(1, 63, (uint8_t*)buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
        return 1;
    }

    oled_clearScreen(OLED_COLOR_BLACK);
    /* ------ operacje na karcie SD ------ */
    /* zbiera i wyswietla pliki znalezione na karcie SD (max 9) */
    for(int i = 1; i < 10; i++) {
        res = f_readdir(&dir, &Finfo);
        if (Finfo.fname[0] == '_' || (Finfo.fattrib & AM_DIR)) {
            i--;
            continue;
        }
        if ((res != FR_OK) || !Finfo.fname[0]) break;
        oled_putString(1, 1 + i * 8, Finfo.fname, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
    };

    /* na razie tego nie tykalem XD */
    acc_read(&x, &y, &z);
    xoff = 0-x;
    yoff = 0-y;
    zoff = 64-z;

    light_enable();

    /* NOWOŚĆ (eksperymentalne): uzywanie przycisku do wlaczania/wylaczania OLEDa (funkcjonalnosc 1)*/
    /* jeszcze nie testowane. */

    uint8_t btn1 = 0;

    GPIO_SetDir(0, 1<<4, 0);

    btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);

        if (btn1 == 0) {
            oled_clearScreen(OLED_COLOR_WHITE);
        	oled_putString(1, 9, (uint8_t*)"btn1 == 0", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
            Timer0_Wait(1000);
        }
        else {
        	sprintf(str, "btn1 = %d", btn1);
            oled_clearScreen(OLED_COLOR_BLACK);
        	oled_putString(1, 9, (uint8_t*)str, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            Timer0_Wait(1000);
        }
        Timer0_Wait(10);
}
