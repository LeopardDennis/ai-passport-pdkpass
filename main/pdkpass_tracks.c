#include "pdkpass_tracks.h"

#include <string.h>

// Compact 48-segment track outlines resampled from the user's local Apex
// resource bundle. Source provenance is documented in the project README.
static const uint8_t s_melbourne[] = {
    103, 3, 97, 3, 91, 3, 85, 3, 80, 7, 75, 10, 69, 9, 63, 10,
    56, 10, 50, 12, 45, 14, 49, 20, 46, 25, 41, 29, 41, 35, 42, 41,
    45, 46, 47, 52, 52, 54, 58, 57, 64, 58, 69, 56, 74, 52, 78, 47,
    81, 42, 83, 36, 87, 31, 93, 28, 98, 27, 105, 26, 111, 27, 115, 32,
    121, 33, 127, 34, 133, 33, 139, 31, 144, 28, 150, 25, 151, 20, 148, 15,
    144, 10, 138, 11, 132, 13, 131, 8, 128, 3, 121, 3, 115, 3, 109, 3,
    103, 3,
};

static const uint8_t s_shanghai[] = {
    81, 24, 84, 31, 86, 38, 89, 45, 92, 52, 96, 58, 102, 57, 102, 51,
    96, 53, 98, 47, 105, 48, 113, 49, 120, 49, 127, 47, 134, 45, 128, 42,
    121, 42, 113, 42, 107, 39, 106, 32, 109, 25, 108, 19, 101, 18, 98, 14,
    103, 10, 110, 10, 118, 9, 125, 9, 130, 11, 134, 15, 138, 10, 135, 4,
    128, 3, 120, 3, 113, 3, 105, 3, 98, 3, 91, 3, 83, 3, 76, 3,
    68, 3, 61, 3, 54, 4, 56, 7, 63, 7, 70, 7, 76, 10, 79, 17,
    81, 24,
};

static const uint8_t s_suzuka[] = {
    111, 58, 118, 58, 124, 58, 131, 58, 138, 58, 145, 58, 151, 55, 151, 49,
    145, 49, 138, 49, 133, 46, 126, 48, 120, 45, 114, 43, 110, 49, 104, 50,
    99, 46, 96, 40, 97, 33, 97, 26, 93, 21, 87, 23, 81, 26, 76, 30,
    72, 35, 73, 29, 74, 22, 71, 16, 66, 12, 59, 10, 53, 10, 46, 12,
    41, 9, 42, 3, 48, 3, 55, 5, 61, 7, 67, 10, 73, 14, 79, 17,
    85, 21, 90, 24, 92, 31, 93, 37, 92, 44, 96, 49, 98, 55, 104, 58,
    111, 58,
};

static const uint8_t s_miami[] = {
    83, 50, 92, 45, 102, 39, 112, 35, 112, 27, 109, 16, 102, 9, 91, 8,
    81, 12, 72, 18, 62, 23, 53, 19, 43, 19, 34, 23, 26, 17, 28, 9,
    39, 9, 49, 7, 60, 8, 71, 9, 82, 7, 92, 4, 103, 3, 113, 4,
    124, 7, 134, 11, 144, 16, 153, 21, 160, 28, 155, 37, 164, 41, 166, 49,
    164, 58, 153, 58, 143, 58, 132, 57, 121, 57, 110, 57, 99, 58, 89, 58,
    78, 57, 67, 57, 56, 57, 45, 58, 43, 52, 53, 49, 63, 53, 74, 54,
    83, 50,
};

static const uint8_t s_montreal[] = {
    145, 28, 151, 25, 157, 21, 163, 21, 168, 18, 163, 13, 158, 9, 151, 6,
    145, 6, 138, 7, 132, 5, 125, 3, 118, 5, 112, 8, 107, 6, 101, 3,
    95, 5, 88, 8, 82, 11, 76, 15, 71, 19, 65, 23, 62, 29, 60, 35,
    55, 40, 49, 44, 44, 48, 38, 51, 31, 54, 24, 55, 27, 58, 34, 56,
    41, 55, 47, 55, 54, 54, 61, 53, 68, 51, 75, 50, 81, 48, 88, 46,
    95, 44, 101, 43, 108, 41, 113, 37, 120, 34, 127, 33, 133, 31, 140, 30,
    145, 28,
};

static const uint8_t s_monaco[] = {
    46, 44, 52, 50, 58, 55, 66, 58, 73, 53, 80, 48, 87, 44, 94, 39,
    101, 36, 109, 32, 116, 27, 124, 28, 128, 35, 130, 43, 138, 45, 147, 46,
    155, 46, 155, 39, 154, 32, 157, 39, 164, 37, 162, 31, 156, 25, 149, 21,
    141, 17, 133, 16, 125, 18, 117, 20, 109, 23, 102, 28, 95, 32, 89, 36,
    82, 41, 76, 47, 69, 51, 61, 50, 54, 45, 51, 37, 47, 30, 41, 25,
    36, 18, 35, 10, 34, 3, 28, 7, 30, 15, 32, 23, 36, 30, 40, 38,
    46, 44,
};

static const uint8_t s_catalunya[] = {
    140, 4, 127, 4, 114, 3, 101, 3, 88, 3, 75, 4, 62, 4, 49, 3,
    36, 3, 28, 11, 20, 21, 9, 27, 4, 39, 9, 50, 21, 56, 34, 57,
    47, 58, 60, 57, 67, 48, 58, 39, 46, 38, 33, 38, 25, 31, 36, 23,
    47, 17, 60, 14, 72, 16, 76, 28, 83, 38, 90, 50, 102, 55, 113, 50,
    125, 43, 136, 36, 147, 30, 158, 23, 170, 22, 167, 34, 156, 40, 153, 51,
    165, 53, 178, 49, 187, 40, 188, 28, 187, 15, 179, 5, 166, 4, 153, 4,
    140, 4,
};

static const uint8_t s_spielberg[] = {
    118, 10, 112, 8, 106, 7, 100, 5, 94, 3, 88, 3, 84, 8, 81, 14,
    77, 19, 74, 24, 71, 30, 68, 35, 65, 41, 61, 46, 56, 50, 52, 55,
    56, 58, 62, 58, 68, 58, 75, 57, 81, 55, 87, 54, 93, 54, 100, 54,
    106, 53, 107, 48, 102, 44, 96, 42, 90, 43, 84, 44, 78, 42, 76, 37,
    79, 31, 83, 26, 89, 25, 94, 29, 99, 32, 105, 33, 111, 33, 118, 33,
    124, 34, 130, 34, 136, 32, 139, 27, 140, 20, 137, 16, 131, 14, 124, 12,
    118, 10,
};

static const uint8_t s_silverstone[] = {
    115, 6, 110, 10, 104, 13, 99, 17, 96, 24, 96, 30, 93, 36, 88, 41,
    92, 45, 92, 49, 85, 50, 80, 46, 76, 40, 71, 35, 67, 29, 63, 24,
    60, 18, 66, 15, 68, 10, 62, 10, 56, 14, 52, 19, 50, 26, 49, 32,
    48, 39, 48, 46, 53, 50, 59, 52, 66, 53, 73, 54, 79, 56, 86, 56,
    93, 57, 99, 58, 103, 53, 109, 49, 115, 47, 122, 44, 128, 41, 134, 38,
    140, 35, 144, 30, 142, 24, 136, 20, 131, 16, 127, 11, 127, 5, 121, 3,
    115, 6,
};

static const uint8_t s_spa[] = {
    66, 19, 61, 16, 56, 12, 52, 13, 55, 19, 59, 24, 63, 28, 68, 33,
    72, 38, 78, 40, 83, 44, 88, 47, 94, 49, 100, 52, 106, 54, 112, 56,
    118, 58, 124, 57, 130, 58, 135, 54, 139, 49, 140, 43, 135, 47, 130, 49,
    124, 46, 118, 45, 112, 43, 109, 38, 112, 32, 117, 29, 124, 28, 129, 24,
    128, 18, 133, 14, 138, 11, 135, 5, 129, 3, 123, 6, 119, 10, 115, 15,
    111, 20, 107, 25, 101, 27, 94, 27, 88, 24, 82, 22, 76, 21, 71, 23,
    66, 19,
};

static const uint8_t s_hungaroring[] = {
    106, 4, 100, 4, 95, 3, 90, 3, 85, 3, 80, 3, 75, 3, 70, 4,
    73, 8, 78, 10, 83, 11, 88, 10, 93, 11, 95, 15, 90, 17, 86, 20,
    85, 25, 84, 30, 83, 35, 83, 40, 82, 45, 78, 49, 74, 52, 74, 56,
    79, 58, 84, 58, 89, 57, 91, 53, 96, 50, 100, 49, 105, 51, 108, 47,
    111, 43, 115, 41, 120, 39, 122, 34, 121, 29, 121, 24, 121, 19, 119, 14,
    114, 15, 109, 15, 107, 12, 112, 11, 117, 11, 119, 7, 116, 3, 111, 3,
    106, 4,
};

static const uint8_t s_zandvoort[] = {
    74, 38, 76, 43, 78, 49, 80, 54, 84, 58, 86, 53, 84, 48, 83, 42,
    81, 37, 76, 35, 78, 31, 84, 33, 89, 34, 95, 33, 101, 33, 106, 35,
    112, 37, 117, 38, 123, 37, 128, 34, 128, 28, 125, 23, 122, 18, 118, 14,
    113, 15, 108, 18, 110, 22, 115, 23, 120, 26, 117, 30, 111, 31, 105, 31,
    100, 29, 94, 28, 89, 26, 84, 24, 79, 24, 80, 18, 81, 13, 81, 7,
    78, 3, 72, 3, 67, 6, 64, 11, 66, 16, 68, 22, 70, 27, 72, 33,
    74, 38,
};

static const uint8_t s_monza[] = {
    128, 3, 122, 3, 116, 3, 110, 3, 104, 3, 98, 3, 92, 3, 86, 3,
    82, 5, 76, 4, 70, 3, 64, 3, 59, 6, 55, 10, 52, 15, 51, 21,
    50, 27, 49, 33, 47, 38, 44, 43, 42, 48, 41, 54, 46, 57, 52, 58,
    57, 57, 61, 52, 64, 48, 67, 43, 71, 38, 76, 34, 80, 30, 84, 26,
    88, 22, 93, 20, 99, 18, 104, 17, 110, 17, 116, 16, 122, 16, 128, 16,
    134, 16, 140, 16, 146, 16, 151, 14, 150, 8, 145, 5, 140, 4, 134, 3,
    128, 3,
};

static const uint8_t s_madring[] = {
    55, 33, 54, 28, 51, 24, 54, 19, 59, 17, 63, 14, 68, 11, 73, 8,
    78, 6, 83, 6, 89, 7, 93, 8, 99, 7, 104, 5, 109, 3, 113, 4,
    119, 3, 124, 4, 128, 7, 133, 5, 138, 6, 141, 11, 138, 15, 133, 16,
    128, 14, 123, 11, 119, 7, 115, 10, 111, 13, 105, 14, 101, 16, 99, 21,
    100, 26, 97, 30, 93, 33, 91, 38, 90, 43, 86, 46, 80, 46, 75, 47,
    75, 52, 73, 56, 68, 57, 62, 58, 59, 55, 58, 49, 57, 44, 56, 38,
    55, 33,
};

static const uint8_t s_baku[] = {
    128, 41, 132, 43, 137, 45, 138, 49, 136, 53, 134, 58, 130, 57, 125, 56,
    120, 54, 115, 52, 111, 50, 106, 49, 101, 47, 103, 42, 103, 37, 98, 36,
    94, 33, 92, 29, 88, 27, 84, 24, 79, 21, 77, 25, 75, 29, 72, 32,
    67, 31, 62, 29, 58, 27, 55, 23, 54, 18, 54, 13, 55, 8, 59, 6,
    63, 3, 67, 3, 70, 8, 73, 11, 77, 14, 79, 19, 82, 23, 87, 25,
    91, 28, 96, 30, 100, 32, 105, 33, 110, 35, 115, 37, 119, 38, 124, 40,
    128, 41,
};

static const uint8_t s_sepang[] = {
    100, 31, 94, 32, 87, 33, 80, 33, 73, 34, 66, 34, 62, 31, 68, 30,
    65, 24, 66, 17, 70, 11, 76, 9, 83, 7, 89, 4, 95, 3, 97, 10,
    98, 17, 103, 21, 109, 20, 115, 16, 121, 20, 125, 25, 130, 30, 130, 36,
    124, 39, 117, 39, 110, 40, 103, 41, 102, 44, 106, 49, 105, 56, 100, 58,
    94, 54, 89, 49, 83, 48, 76, 51, 70, 48, 70, 43, 77, 42, 84, 41,
    91, 40, 98, 38, 104, 37, 111, 36, 118, 35, 121, 30, 114, 30, 107, 31,
    100, 31,
};

static const uint8_t s_singapore[] = {
    137, 14, 138, 20, 140, 26, 142, 32, 143, 39, 139, 44, 135, 43, 133, 37,
    133, 30, 131, 24, 125, 24, 119, 27, 113, 30, 107, 33, 102, 37, 98, 42,
    94, 47, 91, 53, 85, 54, 80, 50, 76, 51, 74, 57, 68, 58, 63, 54,
    57, 50, 52, 47, 49, 41, 52, 36, 51, 30, 54, 24, 57, 21, 61, 27,
    64, 32, 68, 37, 73, 42, 78, 45, 81, 39, 84, 33, 89, 29, 94, 26,
    100, 23, 106, 20, 112, 17, 113, 10, 118, 7, 124, 4, 130, 3, 135, 8,
    137, 14,
};

static const uint8_t s_austin[] = {
    53, 22, 58, 18, 63, 14, 67, 11, 72, 7, 77, 3, 81, 4, 79, 9,
    79, 15, 84, 19, 89, 23, 92, 28, 97, 32, 99, 37, 105, 38, 111, 36,
    115, 40, 121, 39, 127, 39, 132, 42, 136, 46, 140, 51, 144, 56, 141, 58,
    135, 56, 130, 54, 124, 52, 118, 51, 112, 50, 106, 49, 100, 48, 94, 47,
    88, 46, 82, 45, 85, 40, 87, 35, 82, 36, 78, 41, 77, 37, 80, 32,
    79, 26, 74, 23, 68, 25, 65, 30, 60, 34, 55, 32, 49, 30, 48, 25,
    53, 22,
};

static const uint8_t s_mexico_city[] = {
    57, 23, 62, 26, 67, 28, 72, 31, 77, 33, 82, 36, 87, 39, 92, 42,
    97, 44, 102, 47, 107, 50, 112, 53, 117, 55, 122, 58, 127, 57, 131, 54,
    134, 50, 136, 44, 136, 38, 136, 33, 137, 27, 137, 21, 137, 16, 139, 10,
    143, 8, 141, 3, 137, 4, 134, 9, 132, 14, 129, 19, 124, 20, 118, 21,
    113, 21, 108, 18, 102, 19, 97, 20, 91, 19, 86, 17, 81, 15, 76, 12,
    71, 9, 66, 8, 62, 13, 58, 15, 55, 11, 50, 10, 49, 16, 52, 20,
    57, 23,
};

static const uint8_t s_interlagos[] = {
    83, 58, 76, 57, 69, 57, 61, 56, 56, 52, 58, 46, 55, 39, 54, 32,
    58, 26, 64, 22, 71, 19, 77, 16, 84, 13, 90, 10, 97, 6, 103, 3,
    110, 4, 113, 10, 113, 17, 109, 23, 104, 29, 99, 34, 94, 39, 92, 46,
    96, 52, 103, 53, 109, 51, 107, 45, 110, 40, 117, 43, 124, 46, 127, 41,
    120, 38, 117, 32, 119, 25, 124, 20, 130, 15, 135, 19, 138, 26, 137, 33,
    135, 40, 131, 46, 125, 50, 119, 53, 112, 56, 105, 57, 98, 58, 90, 58,
    83, 58,
};

static const uint8_t s_las_vegas[] = {
    137, 50, 132, 54, 128, 58, 125, 54, 128, 49, 126, 44, 120, 42, 114, 42,
    108, 42, 102, 42, 96, 41, 90, 41, 84, 42, 83, 48, 82, 54, 78, 58,
    73, 56, 67, 57, 66, 52, 66, 46, 66, 40, 63, 34, 58, 31, 53, 28,
    50, 22, 50, 17, 56, 15, 61, 12, 67, 9, 72, 7, 78, 5, 84, 4,
    90, 3, 96, 3, 103, 3, 109, 3, 115, 3, 121, 3, 127, 3, 133, 3,
    139, 4, 142, 9, 142, 15, 142, 21, 142, 27, 142, 33, 142, 39, 141, 45,
    137, 50,
};

static const uint8_t s_losail[] = {
    109, 3, 102, 3, 95, 3, 89, 3, 82, 3, 75, 3, 69, 3, 64, 7,
    68, 12, 74, 15, 75, 20, 69, 24, 64, 28, 62, 34, 60, 40, 58, 47,
    62, 51, 68, 51, 71, 45, 73, 39, 77, 40, 78, 46, 78, 53, 82, 58,
    86, 54, 89, 48, 93, 43, 92, 37, 92, 30, 99, 31, 104, 34, 108, 40,
    111, 46, 113, 52, 118, 56, 125, 56, 130, 51, 132, 45, 129, 39, 125, 34,
    121, 29, 124, 23, 128, 18, 132, 12, 134, 6, 129, 3, 122, 3, 115, 3,
    109, 3,
};

static const uint8_t s_yas_marina[] = {
    86, 43, 88, 36, 90, 30, 92, 23, 97, 20, 103, 23, 108, 27, 110, 33,
    115, 38, 121, 38, 128, 38, 135, 40, 141, 41, 147, 45, 142, 48, 135, 48,
    128, 50, 121, 51, 115, 52, 108, 53, 101, 54, 95, 55, 88, 57, 81, 58,
    75, 58, 72, 53, 66, 50, 62, 45, 58, 39, 55, 33, 51, 27, 48, 21,
    47, 15, 45, 8, 49, 3, 54, 7, 54, 13, 54, 20, 55, 27, 60, 31,
    66, 31, 70, 26, 75, 29, 73, 36, 71, 42, 74, 48, 79, 52, 84, 49,
    86, 43,
};

// Derived from bacinger/f1-circuits; MIT notice in assets/images/circuits/LICENSE.txt.
static const uint8_t s_portimao[] = {
    85, 26, 84, 21, 82, 17, 81, 13, 80, 8, 83, 5, 86, 3, 91, 4,
    92, 7, 88, 9, 89, 14, 90, 18, 91, 22, 92, 27, 94, 31, 95, 35,
    97, 31, 97, 27, 96, 22, 94, 18, 93, 14, 96, 11, 98, 12, 99, 16,
    100, 21, 101, 25, 104, 28, 108, 30, 111, 33, 109, 36, 105, 38, 101, 40,
    100, 45, 101, 49, 104, 47, 107, 43, 110, 45, 109, 49, 106, 53, 103, 56,
    99, 57, 95, 55, 92, 52, 91, 47, 90, 43, 89, 39, 87, 34, 86, 30,
    85, 26,
};

// Derived from bacinger/f1-circuits; MIT notice in assets/images/circuits/LICENSE.txt.
static const uint8_t s_istanbul[] = {
    86, 54, 91, 53, 96, 52, 96, 48, 96, 43, 100, 40, 104, 38, 108, 37,
    113, 37, 117, 36, 117, 31, 121, 30, 124, 28, 121, 24, 117, 22, 113, 20,
    109, 18, 106, 15, 109, 12, 114, 13, 118, 14, 123, 14, 126, 11, 125, 7,
    122, 4, 118, 3, 113, 5, 109, 6, 104, 7, 100, 9, 98, 12, 98, 16,
    97, 21, 95, 25, 94, 30, 92, 34, 89, 37, 84, 39, 80, 41, 76, 43,
    72, 46, 68, 48, 65, 51, 68, 52, 68, 57, 73, 56, 77, 55, 82, 54,
    86, 54,
};

// Derived from bacinger/f1-circuits; MIT notice in assets/images/circuits/LICENSE.txt.
static const uint8_t s_sakhir[] = {
    78, 29, 79, 24, 79, 18, 79, 13, 79, 8, 79, 3, 83, 6, 88, 4,
    93, 5, 98, 6, 103, 7, 108, 8, 113, 9, 111, 13, 108, 16, 105, 20,
    101, 22, 96, 24, 93, 28, 90, 29, 91, 24, 91, 19, 92, 14, 88, 11,
    88, 16, 87, 21, 87, 27, 87, 32, 87, 37, 86, 42, 90, 44, 94, 41,
    96, 36, 100, 34, 105, 35, 109, 38, 107, 42, 103, 44, 98, 47, 94, 49,
    89, 52, 85, 54, 80, 57, 77, 54, 77, 49, 78, 44, 78, 39, 78, 34,
    78, 29,
};

// Derived from bacinger/f1-circuits; MIT notice in assets/images/circuits/LICENSE.txt.
static const uint8_t s_jeddah[] = {
    98, 45, 97, 42, 97, 40, 96, 37, 94, 36, 95, 34, 94, 31, 94, 29,
    93, 27, 92, 25, 93, 22, 91, 20, 91, 18, 93, 16, 94, 14, 94, 11,
    94, 9, 94, 6, 94, 4, 92, 3, 92, 6, 93, 8, 93, 11, 92, 13,
    91, 15, 90, 17, 90, 19, 91, 22, 91, 24, 92, 27, 93, 29, 94, 31,
    94, 34, 93, 36, 95, 38, 95, 41, 94, 43, 93, 45, 93, 48, 94, 50,
    95, 53, 97, 54, 99, 56, 101, 57, 101, 54, 100, 52, 100, 49, 99, 47,
    98, 45,
};

// Stable circuit identities: no year, round, meeting or race-lap fields.
static const pdkpass_track_info_t s_catalog[] = {
    { "melbourne", "MELBOURNE", 5278, 0x229971, 0x0871CD, { s_melbourne, sizeof(s_melbourne) / 2U } },
    { "shanghai", "SHANGHAI", 5451, 0xF2A900, 0xC8102E, { s_shanghai, sizeof(s_shanghai) / 2U } },
    { "suzuka", "SUZUKA", 5807, 0x229971, 0xD84A7F, { s_suzuka, sizeof(s_suzuka) / 2U } },
    { "miami", "MIAMI", 5412, 0xD3208B, 0xD5007F, { s_miami, sizeof(s_miami) / 2U } },
    { "montreal", "MONTREAL", 4361, 0xFF8700, 0xD52B1E, { s_montreal, sizeof(s_montreal) / 2U } },
    { "monaco", "MONACO", 3337, 0x229971, 0x00A6C8, { s_monaco, sizeof(s_monaco) / 2U } },
    { "catalunya", "BARCELONA", 4657, 0xFF7A00, 0xFF7A00, { s_catalunya, sizeof(s_catalunya) / 2U } },
    { "spielberg", "SPIELBERG", 4318, 0xD3208B, 0xB50925, { s_spielberg, sizeof(s_spielberg) / 2U } },
    { "silverstone", "SILVERSTONE", 5891, 0x00A6C8, 0x1D4ED8, { s_silverstone, sizeof(s_silverstone) / 2U } },
    { "spa", "SPA-FRANCORCHAMPS", 7004, 0xD3208B, 0x365C7D, { s_spa, sizeof(s_spa) / 2U } },
    { "hungaroring", "HUNGARORING", 4381, 0x8A3FFC, 0x7B2CBF, { s_hungaroring, sizeof(s_hungaroring) / 2U } },
    { "zandvoort", "ZANDVOORT", 4259, 0xD3208B, 0xE85D04, { s_zandvoort, sizeof(s_zandvoort) / 2U } },
    { "monza", "MONZA", 5793, 0xFFD928, 0x59636E, { s_monza, sizeof(s_monza) / 2U } },
    { "madring", "MADRING", 5416, 0xF2A900, 0x5B3FD6, { s_madring, sizeof(s_madring) / 2U } },
    { "baku", "BAKU", 6003, 0x00A6C8, 0x009CA6, { s_baku, sizeof(s_baku) / 2U } },
    { "sepang", "SEPANG", 5543, 0xFF7A00, 0x009B77, { s_sepang, sizeof(s_sepang) / 2U } },
    { "singapore", "MARINA BAY", 4927, 0x8A3FFC, 0x541388, { s_singapore, sizeof(s_singapore) / 2U } },
    { "austin", "COTA", 5513, 0x0057B8, 0x173F5F, { s_austin, sizeof(s_austin) / 2U } },
    { "mexico_city", "MEXICO CITY", 4304, 0x00843D, 0x00843D, { s_mexico_city, sizeof(s_mexico_city) / 2U } },
    { "interlagos", "INTERLAGOS", 4309, 0xFFCC29, 0x4C9F38, { s_interlagos, sizeof(s_interlagos) / 2U } },
    { "las_vegas", "LAS VEGAS", 6201, 0xD3208B, 0xFF2DAA, { s_las_vegas, sizeof(s_las_vegas) / 2U } },
    { "losail", "LUSAIL", 5419, 0x8A1538, 0x8A1538, { s_losail, sizeof(s_losail) / 2U } },
    { "yas_marina", "YAS MARINA", 5281, 0x00A9A5, 0x004C6D, { s_yas_marina, sizeof(s_yas_marina) / 2U } },
    { "portimao", "PORTIMAO", 4653, 0xD4AF37, 0xC49A38, { s_portimao, sizeof(s_portimao) / 2U } },
    { "istanbul", "ISTANBUL", 5338, 0xE32636, 0xB84B40, { s_istanbul, sizeof(s_istanbul) / 2U } },
    { "sakhir", "SAKHIR", 5412, 0xC87941, 0xA87956, { s_sakhir, sizeof(s_sakhir) / 2U } },
    { "jeddah", "JEDDAH", 6175, 0x60B893, 0x487B68, { s_jeddah, sizeof(s_jeddah) / 2U } },
};

typedef struct { const char *alias; const char *id; } track_alias_t;
static const track_alias_t s_aliases[] = {
    { "MELBOURNE", "melbourne" },
    { "ALBERT PARK", "melbourne" },
    { "ALBERT PARK GRAND PRIX CIRCUIT", "melbourne" },
    { "SHANGHAI", "shanghai" },
    { "SHANGHAI INTERNATIONAL CIRCUIT", "shanghai" },
    { "SUZUKA", "suzuka" },
    { "SUZUKA CIRCUIT", "suzuka" },
    { "MIAMI", "miami" },
    { "MIAMI INTERNATIONAL AUTODROME", "miami" },
    { "MONTREAL", "montreal" },
    { "GILLES VILLENEUVE", "montreal" },
    { "CIRCUIT GILLES-VILLENEUVE", "montreal" },
    { "MONACO", "monaco" },
    { "MONTE CARLO", "monaco" },
    { "CIRCUIT DE MONACO", "monaco" },
    { "BARCELONA", "catalunya" },
    { "CATALUNYA", "catalunya" },
    { "CIRCUIT DE BARCELONA-CATALUNYA", "catalunya" },
    { "SPIELBERG", "spielberg" },
    { "RED BULL RING", "spielberg" },
    { "SILVERSTONE", "silverstone" },
    { "SILVERSTONE CIRCUIT", "silverstone" },
    { "SPA", "spa" },
    { "SPA-FRANCORCHAMPS", "spa" },
    { "CIRCUIT DE SPA-FRANCORCHAMPS", "spa" },
    { "BUDAPEST", "hungaroring" },
    { "HUNGARORING", "hungaroring" },
    { "ZANDVOORT", "zandvoort" },
    { "CIRCUIT ZANDVOORT", "zandvoort" },
    { "MONZA", "monza" },
    { "AUTODROMO NAZIONALE MONZA", "monza" },
    { "MADRING", "madring" },
    { "MADRID", "madring" },
    { "BAKU", "baku" },
    { "BAKU CITY CIRCUIT", "baku" },
    { "SEPANG", "sepang" },
    { "SEPANG INTERNATIONAL CIRCUIT", "sepang" },
    { "MARINA BAY", "singapore" },
    { "SINGAPORE", "singapore" },
    { "MARINA BAY STREET CIRCUIT", "singapore" },
    { "COTA", "austin" },
    { "AUSTIN", "austin" },
    { "CIRCUIT OF THE AMERICAS", "austin" },
    { "MEXICO CITY", "mexico_city" },
    { "AUTÓDROMO HERMANOS RODRÍGUEZ", "mexico_city" },
    { "INTERLAGOS", "interlagos" },
    { "SAO PAULO", "interlagos" },
    { "AUTÓDROMO JOSÉ CARLOS PACE", "interlagos" },
    { "LAS VEGAS", "las_vegas" },
    { "LAS VEGAS STRIP CIRCUIT", "las_vegas" },
    { "LUSAIL", "losail" },
    { "LOSAIL", "losail" },
    { "LUSAIL INTERNATIONAL CIRCUIT", "losail" },
    { "YAS MARINA", "yas_marina" },
    { "YAS MARINA CIRCUIT", "yas_marina" },
    { "PORTIMAO", "portimao" },
    { "PORTIMÃO", "portimao" },
    { "ALGARVE", "portimao" },
    { "AUTODROMO INTERNACIONAL DO ALGARVE", "portimao" },
    { "ISTANBUL", "istanbul" },
    { "ISTANBUL PARK", "istanbul" },
    { "INTERCITY ISTANBUL PARK", "istanbul" },
    { "SAKHIR", "sakhir" },
    { "BAHRAIN", "sakhir" },
    { "BAHRAIN INTERNATIONAL CIRCUIT", "sakhir" },
    { "JEDDAH", "jeddah" },
    { "JEDDAH CORNICHE CIRCUIT", "jeddah" },
};

// Normalize ASCII case and Latin accents used by known circuit names.
static unsigned name_char(const unsigned char **cursor)
{
    unsigned c = *(*cursor)++;
    if (c == 0xC3 && **cursor) {
        unsigned next = *(*cursor)++;
        switch (next | 0x20U) {
            case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: return 'A';
            case 0xA7: return 'C';
            case 0xA8: case 0xA9: case 0xAA: case 0xAB: return 'E';
            case 0xAC: case 0xAD: case 0xAE: case 0xAF: return 'I';
            case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: return 'O';
            case 0xB9: case 0xBA: case 0xBB: case 0xBC: return 'U';
            default: return 0xC300U | next;
        }
    }
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

static bool name_equal(const char *a, const char *b)
{
    const unsigned char *left = (const unsigned char *)a;
    const unsigned char *right = (const unsigned char *)b;
    while (*left && *right) if (name_char(&left) != name_char(&right)) return false;
    return *left == *right;
}

size_t pdkpass_track_count(void) { return sizeof(s_catalog) / sizeof(s_catalog[0]); }
const pdkpass_track_info_t *pdkpass_track_at(size_t index)
{
    return index < pdkpass_track_count() ? &s_catalog[index] : NULL;
}

const pdkpass_track_info_t *pdkpass_track_find(const char *circuit)
{
    if (!circuit) return NULL;
    const char *id = circuit;
    for (size_t i = 0; i < sizeof(s_aliases) / sizeof(s_aliases[0]); i++) {
        if (name_equal(circuit, s_aliases[i].alias)) { id = s_aliases[i].id; break; }
    }
    for (size_t i = 0; i < pdkpass_track_count(); i++)
        if (name_equal(id, s_catalog[i].id)) return &s_catalog[i];
    return NULL;
}

bool pdkpass_track_get(const char *circuit, pdkpass_track_geometry_t *geometry)
{
    const pdkpass_track_info_t *track = pdkpass_track_find(circuit);
    if (!track || !geometry) return false;
    *geometry = track->geometry;
    return true;
}
