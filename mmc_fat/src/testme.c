#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "oled.h"
#include <stdio.h>

/* Inicjalizacja przycisku na porcie 0, pin 4.
 * Ustawiamy pin jako GPIO z aktywowanym rezystorem podciągającym (pull-up).
 */
void button_init(void) {
    PINSEL_CFG_Type PinCfg;
    
    PinCfg.Portnum   = 0;
    PinCfg.Pinnum    = 4;
    PinCfg.Funcnum   = 0;       // Tryb GPIO
    PinCfg.Pinmode   = 0;       // Rezystor pull-up włączony
    PinCfg.OpenDrain = 0;
    PINSEL_ConfigPin(&PinCfg);

    // Ustawienie kierunku – pin jako wejście
    GPIO_SetDir(0, (1 << 4), 0);
}

/* Główna funkcja debugująca przycisk */
int main(void) {
    uint8_t btn1 = 1;      // Zakładamy stan nieaktywnego przycisku (podciągnięty do HIGH)
    char str[20];          // Bufor do wyświetlania wartości na OLED

    // Inicjalizacja przycisku oraz wyświetlacza OLED
    button_init();
    oled_init();
    oled_clearScreen(OLED_COLOR_BLACK);

    while (1) {
        // Odczyt stanu przycisku (zakładamy, że przycisk jest aktywny LOW)
        btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
        sprintf(str, "btn1 = %d", btn1);
        oled_putString(1, 9, (uint8_t*)str, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

        // Jeśli przycisk wciśnięty (stan 0), wyświetl dodatkową informację
        if (btn1 == 0) {
            oled_putString(1, 18, (uint8_t*)"Button pressed", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
        } else {
            oled_putString(1, 18, (uint8_t*)"Button not pressed", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
        }

        // Timer0_Wait(100) – wprowadza opóźnienie (np. ~100 ms, zależnie od konfiguracji)
        Timer0_Wait(100);

        // Czyścimy ekran przed kolejnym odświeżeniem
        oled_clearScreen(OLED_COLOR_BLACK);
    }
    
    return 0;
}
