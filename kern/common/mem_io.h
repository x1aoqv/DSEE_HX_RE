#pragma once
#include <stdint.h>
#include <string.h>

static inline uint32_t dsx_ru32(uint8_t *b, int off)
{
    uint32_t v;
    memcpy(&v, b + off, 4);
    return v;
}

static inline void dsx_wu32(uint8_t *b, int off, uint32_t v)
{
    memcpy(b + off, &v, 4);
}

static inline float dsx_rf32(uint8_t *b, int off)
{
    float v;
    memcpy(&v, b + off, 4);
    return v;
}

static inline void dsx_wf32(uint8_t *b, int off, float v)
{
    memcpy(b + off, &v, 4);
}

static inline float *dsx_pf(uint8_t *b, int off)
{
    return (float *)(b + off);
}

static inline float *dsx_F(uint8_t *m, uint32_t off)
{
    return (float *)(m + off);
}
