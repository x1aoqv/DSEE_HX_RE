#pragma once
#include <stdint.h>

#define DSX_INNER 84
#define DSX_SLOT 2164

void dsx_energy4(const float *c, const float *buf, float *out);
void dsx_energy16(const float *src, float *out);
void dsx_agc_f80(uint8_t *mem, int c, float *g, float *out);
void dsx_band_gain_f80(uint8_t *mem, int c);
void dsx_input_decode(uint8_t *mem, int c);
