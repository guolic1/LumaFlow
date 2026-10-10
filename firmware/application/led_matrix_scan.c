#include "led_matrix_scan.h"

#include <stddef.h>
#include <string.h>

#define LED_MATRIX_NO_LED UINT8_MAX

typedef struct
{
    uint8_t anode;
    uint8_t cathode;
} led_connection_t;

typedef struct
{
    uint8_t first;
    uint8_t second;
} led_slot_t;

/* Extracted from circuit/LED_Matrix.kicad_sch through KiCad's netlist export.
 * Each slot is an induced directed matching: no GPIO shares two LED loads,
 * and no additional LED directly connects an enabled source to a sink.
 * Wiring changes require revalidating both tables against the schematic. */
static const led_connection_t connections[LED_MATRIX_LED_COUNT] = {
    {1U, 0U},   /* D1 */
    {2U, 0U},   /* D2 */
    {3U, 0U},   /* D3 */
    {4U, 0U},   /* D4 */
    {5U, 0U},   /* D5 */
    {6U, 0U},   /* D6 */
    {7U, 0U},   /* D7 */
    {8U, 0U},   /* D8 */
    {9U, 0U},   /* D9 */
    {0U, 1U},   /* D10 */
    {2U, 1U},   /* D11 */
    {3U, 1U},   /* D12 */
    {4U, 1U},   /* D13 */
    {5U, 1U},   /* D14 */
    {6U, 1U},   /* D15 */
    {7U, 1U},   /* D16 */
    {8U, 1U},   /* D17 */
    {9U, 1U},   /* D18 */
    {10U, 1U},  /* D19 */
    {0U, 2U},   /* D20 */
    {1U, 2U},   /* D21 */
    {3U, 2U},   /* D22 */
    {4U, 2U},   /* D23 */
    {5U, 2U},   /* D24 */
    {6U, 2U},   /* D25 */
    {7U, 2U},   /* D26 */
    {8U, 2U},   /* D27 */
    {9U, 2U},   /* D28 */
    {10U, 2U},  /* D29 */
    {11U, 2U},  /* D30 */
    {0U, 3U},   /* D31 */
    {1U, 3U},   /* D32 */
    {2U, 3U},   /* D33 */
    {4U, 3U},   /* D34 */
    {5U, 3U},   /* D35 */
    {6U, 3U},   /* D36 */
    {7U, 3U},   /* D37 */
    {8U, 3U},   /* D38 */
    {9U, 3U},   /* D39 */
    {10U, 3U},  /* D40 */
    {11U, 3U},  /* D41 */
    {12U, 3U},  /* D42 */
    {0U, 4U},   /* D43 */
    {1U, 4U},   /* D44 */
    {2U, 4U},   /* D45 */
    {3U, 4U},   /* D46 */
    {5U, 4U},   /* D47 */
    {6U, 4U},   /* D48 */
    {7U, 4U},   /* D49 */
    {8U, 4U},   /* D50 */
    {9U, 4U},   /* D51 */
    {10U, 4U},  /* D52 */
    {11U, 4U},  /* D53 */
    {12U, 4U},  /* D54 */
    {13U, 4U},  /* D55 */
    {0U, 5U},   /* D56 */
    {1U, 5U},   /* D57 */
    {2U, 5U},   /* D58 */
    {3U, 5U},   /* D59 */
    {4U, 5U},   /* D60 */
    {6U, 5U},   /* D61 */
    {7U, 5U},   /* D62 */
    {8U, 5U},   /* D63 */
    {9U, 5U},   /* D64 */
    {10U, 5U},  /* D65 */
    {11U, 5U},  /* D66 */
    {12U, 5U},  /* D67 */
    {13U, 5U},  /* D68 */
    {14U, 5U},  /* D69 */
    {0U, 6U},   /* D70 */
    {1U, 6U},   /* D71 */
    {2U, 6U},   /* D72 */
    {3U, 6U},   /* D73 */
    {4U, 6U},   /* D74 */
    {5U, 6U},   /* D75 */
    {7U, 6U},   /* D76 */
    {8U, 6U},   /* D77 */
    {9U, 6U},   /* D78 */
    {10U, 6U},  /* D79 */
    {11U, 6U},  /* D80 */
    {12U, 6U},  /* D81 */
    {13U, 6U},  /* D82 */
    {14U, 6U},  /* D83 */
    {15U, 6U},  /* D84 */
    {0U, 7U},   /* D85 */
    {1U, 7U},   /* D86 */
    {2U, 7U},   /* D87 */
    {3U, 7U},   /* D88 */
    {4U, 7U},   /* D89 */
    {5U, 7U},   /* D90 */
    {6U, 7U},   /* D91 */
    {8U, 7U},   /* D92 */
    {9U, 7U},   /* D93 */
    {10U, 7U},  /* D94 */
    {11U, 7U},  /* D95 */
    {12U, 7U},  /* D96 */
    {13U, 7U},  /* D97 */
    {14U, 7U},  /* D98 */
    {15U, 7U},  /* D99 */
    {16U, 7U},  /* D100 */
    {0U, 8U},   /* D101 */
    {1U, 8U},   /* D102 */
    {2U, 8U},   /* D103 */
    {3U, 8U},   /* D104 */
    {4U, 8U},   /* D105 */
    {5U, 8U},   /* D106 */
    {6U, 8U},   /* D107 */
    {7U, 8U},   /* D108 */
    {9U, 8U},   /* D109 */
    {10U, 8U},  /* D110 */
    {11U, 8U},  /* D111 */
    {12U, 8U},  /* D112 */
    {13U, 8U},  /* D113 */
    {14U, 8U},  /* D114 */
    {15U, 8U},  /* D115 */
    {16U, 8U},  /* D116 */
    {17U, 8U},  /* D117 */
    {1U, 9U},   /* D118 */
    {2U, 9U},   /* D119 */
    {3U, 9U},   /* D120 */
    {4U, 9U},   /* D121 */
    {5U, 9U},   /* D122 */
    {6U, 9U},   /* D123 */
    {7U, 9U},   /* D124 */
    {8U, 9U},   /* D125 */
    {10U, 9U},  /* D126 */
    {11U, 9U},  /* D127 */
    {12U, 9U},  /* D128 */
    {13U, 9U},  /* D129 */
    {14U, 9U},  /* D130 */
    {15U, 9U},  /* D131 */
    {16U, 9U},  /* D132 */
    {17U, 9U},  /* D133 */
    {2U, 10U},  /* D134 */
    {3U, 10U},  /* D135 */
    {4U, 10U},  /* D136 */
    {5U, 10U},  /* D137 */
    {6U, 10U},  /* D138 */
    {7U, 10U},  /* D139 */
    {8U, 10U},  /* D140 */
    {9U, 10U},  /* D141 */
    {11U, 10U}, /* D142 */
    {12U, 10U}, /* D143 */
    {13U, 10U}, /* D144 */
    {14U, 10U}, /* D145 */
    {15U, 10U}, /* D146 */
    {16U, 10U}, /* D147 */
    {17U, 10U}, /* D148 */
    {3U, 11U},  /* D149 */
    {4U, 11U},  /* D150 */
    {5U, 11U},  /* D151 */
    {6U, 11U},  /* D152 */
    {7U, 11U},  /* D153 */
    {8U, 11U},  /* D154 */
    {9U, 11U},  /* D155 */
    {10U, 11U}, /* D156 */
    {12U, 11U}, /* D157 */
    {13U, 11U}, /* D158 */
    {14U, 11U}, /* D159 */
    {15U, 11U}, /* D160 */
    {16U, 11U}, /* D161 */
    {17U, 11U}, /* D162 */
    {4U, 12U},  /* D163 */
    {5U, 12U},  /* D164 */
    {6U, 12U},  /* D165 */
    {7U, 12U},  /* D166 */
    {8U, 12U},  /* D167 */
    {9U, 12U},  /* D168 */
    {10U, 12U}, /* D169 */
    {11U, 12U}, /* D170 */
    {13U, 12U}, /* D171 */
    {14U, 12U}, /* D172 */
    {15U, 12U}, /* D173 */
    {16U, 12U}, /* D174 */
    {17U, 12U}, /* D175 */
    {5U, 13U},  /* D176 */
    {6U, 13U},  /* D177 */
    {7U, 13U},  /* D178 */
    {8U, 13U},  /* D179 */
    {9U, 13U},  /* D180 */
    {10U, 13U}, /* D181 */
    {11U, 13U}, /* D182 */
    {12U, 13U}, /* D183 */
    {14U, 13U}, /* D184 */
    {15U, 13U}, /* D185 */
    {16U, 13U}, /* D186 */
    {17U, 13U}, /* D187 */
    {6U, 14U},  /* D188 */
    {7U, 14U},  /* D189 */
    {8U, 14U},  /* D190 */
    {9U, 14U},  /* D191 */
    {10U, 14U}, /* D192 */
    {11U, 14U}, /* D193 */
    {12U, 14U}, /* D194 */
    {13U, 14U}, /* D195 */
    {15U, 14U}, /* D196 */
    {16U, 14U}, /* D197 */
    {17U, 14U}, /* D198 */
    {7U, 15U},  /* D199 */
    {8U, 15U},  /* D200 */
    {9U, 15U},  /* D201 */
    {10U, 15U}, /* D202 */
    {11U, 15U}, /* D203 */
    {12U, 15U}, /* D204 */
    {13U, 15U}, /* D205 */
    {14U, 15U}, /* D206 */
    {16U, 15U}, /* D207 */
    {17U, 15U}, /* D208 */
    {8U, 16U},  /* D209 */
    {9U, 16U},  /* D210 */
    {10U, 16U}, /* D211 */
    {11U, 16U}, /* D212 */
    {12U, 16U}, /* D213 */
    {13U, 16U}, /* D214 */
    {14U, 16U}, /* D215 */
    {15U, 16U}, /* D216 */
    {17U, 16U}, /* D217 */
};

static const led_slot_t slots[LED_MATRIX_SLOT_COUNT] = {
    {9U, 126U},                /* D10, D127 */
    {19U, 127U},               /* D20, D128 */
    {30U, 128U},               /* D31, D129 */
    {42U, 129U},               /* D43, D130 */
    {55U, 130U},               /* D56, D131 */
    {69U, 131U},               /* D70, D132 */
    {84U, 132U},               /* D85, D133 */
    {0U, 141U},                /* D1, D142 */
    {20U, 142U},               /* D21, D143 */
    {31U, 143U},               /* D32, D144 */
    {43U, 144U},               /* D44, D145 */
    {56U, 145U},               /* D57, D146 */
    {70U, 146U},               /* D71, D147 */
    {85U, 147U},               /* D86, D148 */
    {1U, 155U},                /* D2, D156 */
    {10U, 156U},               /* D11, D157 */
    {32U, 157U},               /* D33, D158 */
    {44U, 158U},               /* D45, D159 */
    {57U, 159U},               /* D58, D160 */
    {71U, 160U},               /* D72, D161 */
    {86U, 161U},               /* D87, D162 */
    {2U, 168U},                /* D3, D169 */
    {11U, 169U},               /* D12, D170 */
    {21U, 170U},               /* D22, D171 */
    {45U, 171U},               /* D46, D172 */
    {58U, 172U},               /* D59, D173 */
    {72U, 173U},               /* D73, D174 */
    {87U, 174U},               /* D88, D175 */
    {3U, 180U},                /* D4, D181 */
    {12U, 181U},               /* D13, D182 */
    {22U, 182U},               /* D23, D183 */
    {33U, 183U},               /* D34, D184 */
    {59U, 184U},               /* D60, D185 */
    {73U, 185U},               /* D74, D186 */
    {88U, 186U},               /* D89, D187 */
    {4U, 191U},                /* D5, D192 */
    {13U, 192U},               /* D14, D193 */
    {23U, 193U},               /* D24, D194 */
    {34U, 194U},               /* D35, D195 */
    {46U, 195U},               /* D47, D196 */
    {74U, 196U},               /* D75, D197 */
    {89U, 197U},               /* D90, D198 */
    {5U, 201U},                /* D6, D202 */
    {14U, 202U},               /* D15, D203 */
    {24U, 203U},               /* D25, D204 */
    {35U, 204U},               /* D36, D205 */
    {47U, 205U},               /* D48, D206 */
    {60U, 206U},               /* D61, D207 */
    {90U, 207U},               /* D91, D208 */
    {6U, 210U},                /* D7, D211 */
    {15U, 211U},               /* D16, D212 */
    {25U, 212U},               /* D26, D213 */
    {36U, 213U},               /* D37, D214 */
    {48U, 214U},               /* D49, D215 */
    {61U, 215U},               /* D62, D216 */
    {75U, 216U},               /* D76, D217 */
    {7U, LED_MATRIX_NO_LED},   /* D8 */
    {8U, LED_MATRIX_NO_LED},   /* D9 */
    {16U, LED_MATRIX_NO_LED},  /* D17 */
    {17U, LED_MATRIX_NO_LED},  /* D18 */
    {18U, LED_MATRIX_NO_LED},  /* D19 */
    {26U, LED_MATRIX_NO_LED},  /* D27 */
    {27U, LED_MATRIX_NO_LED},  /* D28 */
    {28U, LED_MATRIX_NO_LED},  /* D29 */
    {29U, LED_MATRIX_NO_LED},  /* D30 */
    {37U, LED_MATRIX_NO_LED},  /* D38 */
    {38U, LED_MATRIX_NO_LED},  /* D39 */
    {39U, LED_MATRIX_NO_LED},  /* D40 */
    {40U, LED_MATRIX_NO_LED},  /* D41 */
    {41U, LED_MATRIX_NO_LED},  /* D42 */
    {49U, LED_MATRIX_NO_LED},  /* D50 */
    {50U, LED_MATRIX_NO_LED},  /* D51 */
    {51U, LED_MATRIX_NO_LED},  /* D52 */
    {52U, LED_MATRIX_NO_LED},  /* D53 */
    {53U, LED_MATRIX_NO_LED},  /* D54 */
    {54U, LED_MATRIX_NO_LED},  /* D55 */
    {62U, LED_MATRIX_NO_LED},  /* D63 */
    {63U, LED_MATRIX_NO_LED},  /* D64 */
    {64U, LED_MATRIX_NO_LED},  /* D65 */
    {65U, LED_MATRIX_NO_LED},  /* D66 */
    {66U, LED_MATRIX_NO_LED},  /* D67 */
    {67U, LED_MATRIX_NO_LED},  /* D68 */
    {68U, LED_MATRIX_NO_LED},  /* D69 */
    {76U, LED_MATRIX_NO_LED},  /* D77 */
    {77U, LED_MATRIX_NO_LED},  /* D78 */
    {78U, LED_MATRIX_NO_LED},  /* D79 */
    {79U, LED_MATRIX_NO_LED},  /* D80 */
    {80U, LED_MATRIX_NO_LED},  /* D81 */
    {81U, LED_MATRIX_NO_LED},  /* D82 */
    {82U, LED_MATRIX_NO_LED},  /* D83 */
    {83U, LED_MATRIX_NO_LED},  /* D84 */
    {91U, LED_MATRIX_NO_LED},  /* D92 */
    {92U, LED_MATRIX_NO_LED},  /* D93 */
    {93U, LED_MATRIX_NO_LED},  /* D94 */
    {94U, LED_MATRIX_NO_LED},  /* D95 */
    {95U, LED_MATRIX_NO_LED},  /* D96 */
    {96U, LED_MATRIX_NO_LED},  /* D97 */
    {97U, LED_MATRIX_NO_LED},  /* D98 */
    {98U, LED_MATRIX_NO_LED},  /* D99 */
    {99U, LED_MATRIX_NO_LED},  /* D100 */
    {100U, LED_MATRIX_NO_LED}, /* D101 */
    {101U, LED_MATRIX_NO_LED}, /* D102 */
    {102U, LED_MATRIX_NO_LED}, /* D103 */
    {103U, LED_MATRIX_NO_LED}, /* D104 */
    {104U, LED_MATRIX_NO_LED}, /* D105 */
    {105U, LED_MATRIX_NO_LED}, /* D106 */
    {106U, LED_MATRIX_NO_LED}, /* D107 */
    {107U, LED_MATRIX_NO_LED}, /* D108 */
    {108U, LED_MATRIX_NO_LED}, /* D109 */
    {109U, LED_MATRIX_NO_LED}, /* D110 */
    {110U, LED_MATRIX_NO_LED}, /* D111 */
    {111U, LED_MATRIX_NO_LED}, /* D112 */
    {112U, LED_MATRIX_NO_LED}, /* D113 */
    {113U, LED_MATRIX_NO_LED}, /* D114 */
    {114U, LED_MATRIX_NO_LED}, /* D115 */
    {115U, LED_MATRIX_NO_LED}, /* D116 */
    {116U, LED_MATRIX_NO_LED}, /* D117 */
    {117U, LED_MATRIX_NO_LED}, /* D118 */
    {118U, LED_MATRIX_NO_LED}, /* D119 */
    {119U, LED_MATRIX_NO_LED}, /* D120 */
    {120U, LED_MATRIX_NO_LED}, /* D121 */
    {121U, LED_MATRIX_NO_LED}, /* D122 */
    {122U, LED_MATRIX_NO_LED}, /* D123 */
    {123U, LED_MATRIX_NO_LED}, /* D124 */
    {124U, LED_MATRIX_NO_LED}, /* D125 */
    {125U, LED_MATRIX_NO_LED}, /* D126 */
    {133U, LED_MATRIX_NO_LED}, /* D134 */
    {134U, LED_MATRIX_NO_LED}, /* D135 */
    {135U, LED_MATRIX_NO_LED}, /* D136 */
    {136U, LED_MATRIX_NO_LED}, /* D137 */
    {137U, LED_MATRIX_NO_LED}, /* D138 */
    {138U, LED_MATRIX_NO_LED}, /* D139 */
    {139U, LED_MATRIX_NO_LED}, /* D140 */
    {140U, LED_MATRIX_NO_LED}, /* D141 */
    {148U, LED_MATRIX_NO_LED}, /* D149 */
    {149U, LED_MATRIX_NO_LED}, /* D150 */
    {150U, LED_MATRIX_NO_LED}, /* D151 */
    {151U, LED_MATRIX_NO_LED}, /* D152 */
    {152U, LED_MATRIX_NO_LED}, /* D153 */
    {153U, LED_MATRIX_NO_LED}, /* D154 */
    {154U, LED_MATRIX_NO_LED}, /* D155 */
    {162U, LED_MATRIX_NO_LED}, /* D163 */
    {163U, LED_MATRIX_NO_LED}, /* D164 */
    {164U, LED_MATRIX_NO_LED}, /* D165 */
    {165U, LED_MATRIX_NO_LED}, /* D166 */
    {166U, LED_MATRIX_NO_LED}, /* D167 */
    {167U, LED_MATRIX_NO_LED}, /* D168 */
    {175U, LED_MATRIX_NO_LED}, /* D176 */
    {176U, LED_MATRIX_NO_LED}, /* D177 */
    {177U, LED_MATRIX_NO_LED}, /* D178 */
    {178U, LED_MATRIX_NO_LED}, /* D179 */
    {179U, LED_MATRIX_NO_LED}, /* D180 */
    {187U, LED_MATRIX_NO_LED}, /* D188 */
    {188U, LED_MATRIX_NO_LED}, /* D189 */
    {189U, LED_MATRIX_NO_LED}, /* D190 */
    {190U, LED_MATRIX_NO_LED}, /* D191 */
    {198U, LED_MATRIX_NO_LED}, /* D199 */
    {199U, LED_MATRIX_NO_LED}, /* D200 */
    {200U, LED_MATRIX_NO_LED}, /* D201 */
    {208U, LED_MATRIX_NO_LED}, /* D209 */
    {209U, LED_MATRIX_NO_LED}, /* D210 */
};

void led_matrix_scan_init(led_matrix_scan_t *scan)
{
    memset(scan->frames, 0, sizeof(scan->frames));
    scan->slot = 0U;
    scan->active_frame = 0U;
    atomic_init(&scan->pending, false);
}

bool led_matrix_scan_submit(led_matrix_scan_t *scan, const bool pixels[LED_MATRIX_LED_COUNT])
{
    if (pixels == NULL || atomic_load_explicit(&scan->pending, memory_order_acquire))
    {
        return false;
    }

    memcpy(scan->frames[scan->active_frame ^ 1U], pixels, sizeof(scan->frames[0]));
    /* Publish only after the complete back buffer is available to the ISR. */
    atomic_store_explicit(&scan->pending, true, memory_order_release);
    return true;
}

led_matrix_drive_t led_matrix_scan_next(led_matrix_scan_t *scan)
{
    led_matrix_drive_t drive = {0U, 0U};

    if (scan->slot == 0U && atomic_load_explicit(&scan->pending, memory_order_acquire))
    {
        scan->active_frame ^= 1U;
        atomic_store_explicit(&scan->pending, false, memory_order_release);
    }

    const led_slot_t *slot = &slots[scan->slot];
    const uint8_t indices[2] = {slot->first, slot->second};
    for (unsigned int item = 0U; item < 2U; ++item)
    {
        uint8_t index = indices[item];
        if (index != LED_MATRIX_NO_LED && scan->frames[scan->active_frame][index])
        {
            drive.high |= 1UL << connections[index].anode;
            drive.low |= 1UL << connections[index].cathode;
        }
    }

    if (++scan->slot == LED_MATRIX_SLOT_COUNT)
    {
        scan->slot = 0U;
    }
    return drive;
}
