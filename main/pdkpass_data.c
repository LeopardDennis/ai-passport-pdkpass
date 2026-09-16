#include "pdkpass_data.h"
#include <string.h>

// Offline snapshot captured on 2026-08-31 from the official Formula 1 calendar
// and driver standings. Times below are converted to China Standard Time (UTC+8).
// Schedule: https://www.formula1.com/en/racing/2026
// Standings: https://www.formula1.com/en/results/2026/drivers
const pdkpass_race_t pdkpass_races[] = {
    // R1-R12 restored from OpenF1 meetings/sessions on 2026-09-16.
    // Cancelled Bahrain/Saudi meetings and pre-season tests are excluded.
    { 1279, 1772949600, 0x229971, 5278, 1, 58, "AUSTRALIA", "MELBOURNE", "06-08 MAR", "FP1   06 MAR 09:30", "QUALI 07 MAR 13:00", "RACE  08 MAR 12:00", "Australia" },
    { 1280, 1773565200, 0xF2A900, 5451, 2, 56, "CHINA", "SHANGHAI", "13-15 MAR", "FP1   13 MAR 11:30", "SPR Q 13 MAR 15:30", "RACE  15 MAR 15:00", "China" },
    { 1281, 1774767600, 0x229971, 5807, 3, 53, "JAPAN", "SUZUKA", "27-29 MAR", "FP1   27 MAR 10:30", "QUALI 28 MAR 14:00", "RACE  29 MAR 13:00", "Japan" },
    { 1284, 1777834800, 0xD3208B, 5412, 4, 57, "USA", "MIAMI", "01-03 MAY", "FP1   02 MAY 00:00", "SPR Q 02 MAY 04:30", "RACE  04 MAY 01:00", "United%20States" },
    { 1285, 1779660000, 0xFF8700, 4361, 5, 70, "CANADA", "MONTREAL", "22-24 MAY", "FP1   23 MAY 00:30", "SPR Q 23 MAY 04:30", "RACE  25 MAY 04:00", "Canada" },
    { 1286, 1780844400, 0x229971, 3337, 6, 78, "MONACO", "MONACO", "05-07 JUN", "FP1   05 JUN 19:30", "QUALI 06 JUN 22:00", "RACE  07 JUN 21:00", "Monaco" },
    { 1287, 1781449200, 0xFF7A00, 4657, 7, 66, "SPAIN", "BARCELONA", "12-14 JUN", "FP1   12 JUN 19:30", "QUALI 13 JUN 22:00", "RACE  14 JUN 21:00", "Spain" },
    { 1288, 1782658800, 0xD3208B, 4318, 8, 71, "AUSTRIA", "SPIELBERG", "26-28 JUN", "FP1   26 JUN 19:30", "QUALI 27 JUN 22:00", "RACE  28 JUN 21:00", "Austria" },
    { 1289, 1783267200, 0x00A6C8, 5891, 9, 52, "BRITAIN", "SILVERSTONE", "03-05 JUL", "FP1   03 JUL 19:30", "SPR Q 03 JUL 23:30", "RACE  05 JUL 22:00", "United%20Kingdom" },
    { 1290, 1784473200, 0xD3208B, 7004, 10, 44, "BELGIUM", "SPA-FRANCORCHAMPS", "17-19 JUL", "FP1   17 JUL 19:30", "QUALI 18 JUL 22:00", "RACE  19 JUL 21:00", "Belgium" },
    { 1291, 1785078000, 0x8A3FFC, 4381, 11, 70, "HUNGARY", "HUNGARORING", "24-26 JUL", "FP1   24 JUL 19:30", "QUALI 25 JUL 22:00", "RACE  26 JUL 21:00", "Hungary" },
    { 1292, 1787497200, 0xD3208B, 4259, 12, 72, "NETHERLANDS", "ZANDVOORT", "21-23 AUG", "FP1   21 AUG 18:30", "SPR Q 21 AUG 22:30", "RACE  23 AUG 21:00", "Netherlands" },
    { 0, 1788714000, 0xFFD928, 5793, 13, 53, "ITALY", "MONZA", "04-06 SEP", "FP1   04 SEP 18:30", "QUALI 05 SEP 22:00", "RACE  06 SEP 21:00", "Italy" },
    { 0, 1789318800, 0xF2A900, 5416, 14, 57, "SPAIN", "MADRING", "11-13 SEP", "FP1   11 SEP 19:30", "QUALI 12 SEP 22:00", "RACE  13 SEP 21:00", "Spain" },
    { 0, 1790434800, 0x00A6C8, 6003, 15, 51, "AZERBAIJAN", "BAKU", "24-26 SEP", "FP1   24 SEP 16:30", "QUALI 25 SEP 20:00", "RACE  26 SEP 19:00", "Azerbaijan" },
    { 0, 1791111600, 0xFF7A00, 5543, 16, 56, "MALAYSIA", "SEPANG", "02-04 OCT", "FP1   02 OCT 12:30", "QUALI 03 OCT 16:00", "RACE  04 OCT 15:00", "Malaysia" },
    { 0, 1791734400, 0x8A3FFC, 4927, 17, 62, "SINGAPORE", "MARINA BAY", "09-11 OCT", "FP1   09 OCT 16:30", "SPR Q 09 OCT 20:30", "RACE  11 OCT 20:00", "Singapore" },
    { 0, 1792972800, 0x0057B8, 5513, 18, 56, "USA", "COTA", "23-25 OCT", "FP1   24 OCT 01:30", "QUALI 25 OCT 05:00", "RACE  26 OCT 04:00", "United%20States" },
    { 0, 1793577600, 0x00843D, 4304, 19, 71, "MEXICO", "MEXICO CITY", "30 OCT-01 NOV", "FP1   31 OCT 02:30", "QUALI 01 NOV 05:00", "RACE  02 NOV 04:00", "Mexico" },
    { 0, 1794171600, 0xFFCC29, 4309, 20, 71, "BRAZIL", "INTERLAGOS", "06-08 NOV", "FP1   06 NOV 23:30", "QUALI 08 NOV 02:00", "RACE  09 NOV 01:00", "Brazil" },
    { 0, 1795334400, 0xD3208B, 6201, 21, 50, "LAS VEGAS", "LAS VEGAS", "19-21 NOV", "FP1   20 NOV 08:30", "QUALI 21 NOV 12:00", "RACE  22 NOV 12:00", "United%20States" },
    { 0, 1795982400, 0x8A1538, 5419, 22, 57, "QATAR", "LUSAIL", "27-29 NOV", "FP1   27 NOV 21:30", "QUALI 29 NOV 02:00", "RACE  30 NOV 00:00", "Qatar" },
    { 0, 1796576400, 0x00A9A5, 5281, 23, 58, "ABU DHABI", "YAS MARINA", "04-06 DEC", "FP1   04 DEC 17:30", "QUALI 05 DEC 22:00", "RACE  06 DEC 21:00", "United%20Arab%20Emirates" },
};

const size_t pdkpass_race_count = sizeof(pdkpass_races) / sizeof(pdkpass_races[0]);

size_t pdkpass_restore_legacy_calendar(unsigned year, pdkpass_race_t *races,
                                       size_t count, size_t capacity)
{
    // Only migrate the known R13-R23 fallback shape. A shorter downloaded
    // calendar may represent cancellations; never resurrect those meetings.
    const size_t missing = 12U;
    if (!races || year != 2026U || count != 11U ||
        capacity < pdkpass_race_count) return count;
    for (size_t i = 0; i < count; i++) {
        const pdkpass_race_t *known = &pdkpass_races[missing + i];
        if (races[i].round != known->round ||
            strncmp(races[i].circuit, known->circuit,
                    sizeof(races[i].circuit)) != 0) return count;
    }
    if (races[0].switch_at_utc <= pdkpass_races[missing - 1U].switch_at_utc)
        return count;
    memmove(races + missing, races, count * sizeof(*races));
    memcpy(races, pdkpass_races, missing * sizeof(*races));
    return pdkpass_race_count;
}

const pdkpass_driver_t pdkpass_drivers[] = {
    { 0x00A19C, 2420,  1, 12, "ANT", "ANTONELLI",  "MERCEDES" },
    { 0x00A19C, 1830,  2, 63, "RUS", "RUSSELL",    "MERCEDES" },
    { 0xE32636, 1830,  3, 44, "HAM", "HAMILTON",   "FERRARI" },
    { 0xFF8700, 1590,  4,  1, "NOR", "NORRIS",     "MCLAREN" },
    { 0xE32636, 1550,  5, 16, "LEC", "LECLERC",    "FERRARI" },
    { 0x3671C6, 1120,  6,  3, "VER", "VERSTAPPEN", "RED BULL" },
    { 0xFF8700, 1040,  7, 81, "PIA", "PIASTRI",    "MCLAREN" },
    { 0x3671C6,  680,  8,  6, "HAD", "HADJAR",     "RED BULL" },
    { 0x3671C6,  490,  9, 30, "LAW", "LAWSON",     "RED BULL" },
    { 0x2293D1,  440, 10, 10, "GAS", "GASLY",      "ALPINE" },
    { 0x6692FF,  230, 11, 41, "LIN", "LINDBLAD",   "RACING BULLS" },
    { 0x2293D1,  190, 12, 43, "COL", "COLAPINTO",  "ALPINE" },
    { 0xB6BABD,  180, 13, 87, "BEA", "BEARMAN",    "HAAS" },
    { 0xF50537,  100, 14,  5, "BOR", "BORTOLETO",  "AUDI" },
    { 0xF50537,   60, 15, 27, "HUL", "HULKENBERG", "AUDI" },
    { 0x64C4FF,   60, 16, 55, "SAI", "SAINZ",      "WILLIAMS" },
    { 0x64C4FF,   50, 17, 23, "ALB", "ALBON",      "WILLIAMS" },
    { 0xB6BABD,   30, 18, 31, "OCO", "OCON",       "HAAS" },
    { 0x229971,   30, 19, 14, "ALO", "ALONSO",     "ASTON MARTIN" },
    { 0x6692FF,    0, 20, 22, "TSU", "TSUNODA",    "RACING BULLS" },
    { 0x229971,    0, 21, 18, "STR", "STROLL",     "ASTON MARTIN" },
    { 0x1B2D57,    0, 22, 77, "BOT", "BOTTAS",     "CADILLAC" },
    { 0x1B2D57,    0, 23, 11, "PER", "PEREZ",      "CADILLAC" },
};

const size_t pdkpass_driver_count = sizeof(pdkpass_drivers) / sizeof(pdkpass_drivers[0]);
