#pragma once
#include <stdint.h>

typedef struct CbPair {
    int16_t id;
    int16_t th;
} CbPair;

typedef struct CbEnt {
    const CbPair *p;
    int n;
} CbEnt;

int dsee_pick_level(int channels, int vbr, int content_hz, int src_hz,
                    int codec, unsigned energy, int bit_override,
                    int *level);

const CbEnt *dsx_select_codebook_root(int channels, int vbr, int content_idx);
void dsx_clamp_level(int *level, unsigned codec, int content_idx);
