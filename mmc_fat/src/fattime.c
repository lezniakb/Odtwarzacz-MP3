#include "ff.h"

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