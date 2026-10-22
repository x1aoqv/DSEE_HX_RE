#pragma once
#include <stdint.h>

#define DSX_SLOT 2164

float x87_mul(float a, float b);
float x87_add(float a, float b);
float x87_sub(float a, float b);
float x87_mac(float acc, float coef, float x);
float x87_sqadd(float acc, float x);
float x87_addmul(float c, float d, float e);
float x87_diff25(float mid, float neu, float old);
float x87_env(float env, float x, float a);
float x87_midside(float h1, float h3, float g);

void dsx_gather(const float *src, float *dst, int n, int ch, int nslots);
void dsx_clear_f(float *p, int n);
void dsx_copy_f(float *dst, const float *src, int n);
void dsx_biquad_cascade(uint8_t *mem, uint32_t block, float *src, float *dst,
                        float *state);
void dsx_energy_tap22(uint8_t *mem, uint32_t coef, uint32_t buf, uint32_t out,
                      int nout);
void dsx_envelope_ms(uint8_t *mem, int nslots, int nframe, uint32_t coef,
                     uint32_t state, float a5, const uint32_t *a6,
                     const uint32_t *a7);
void dsx_band_gain8(uint8_t *mem, uint32_t ctx, uint32_t meta, uint32_t coefs,
                    int flag, uint32_t base);
void dsx_synth_fir22(uint8_t *mem, uint32_t coef, uint32_t meta, uint32_t src,
                     uint32_t dst);
void dsx_writeback(float *src, float *dst, int n, int upmode, int ch,
                   int nslots);
