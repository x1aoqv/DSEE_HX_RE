#pragma once

#include <stdint.h>

typedef struct Ext80 {
    int sign;
    uint64_t mant;
    int exp;
} Ext80;

Ext80 ext80_zero(int sign);
Ext80 ext80_from_f64(double x);
Ext80 ext80_from_int(int v);
float ext80_to_f32(Ext80 v);
Ext80 ext80_add(Ext80 a, Ext80 b);
Ext80 ext80_sub(Ext80 a, Ext80 b);
Ext80 ext80_mul(Ext80 a, Ext80 b);
