#include "band_frame.h"
#include "ext80.h"

float x87_mul(float a, float b)
{
    return ext80_to_f32(ext80_mul(ext80_from_f64((double)a), ext80_from_f64((double)b)));
}

float x87_add(float a, float b)
{
    return ext80_to_f32(ext80_add(ext80_from_f64((double)a), ext80_from_f64((double)b)));
}

float x87_sub(float a, float b)
{
    return ext80_to_f32(ext80_sub(ext80_from_f64((double)a), ext80_from_f64((double)b)));
}

float x87_mac(float acc, float coef, float x)
{
    Ext80 t = ext80_mul(ext80_from_f64((double)coef), ext80_from_f64((double)x));
    return ext80_to_f32(ext80_add(t, ext80_from_f64((double)acc)));
}

float x87_sqadd(float acc, float x)
{
    Ext80 t = ext80_mul(ext80_from_f64((double)x), ext80_from_f64((double)x));
    return ext80_to_f32(ext80_add(t, ext80_from_f64((double)acc)));
}

float x87_addmul(float c, float d, float e)
{
    Ext80 s = ext80_add(ext80_from_f64((double)c), ext80_from_f64((double)d));
    return ext80_to_f32(ext80_mul(s, ext80_from_f64((double)e)));
}

float x87_diff25(float mid, float neu, float old)
{
    Ext80 left = ext80_mul(ext80_from_f64((double)mid), ext80_from_f64(0.5));
    Ext80 right = ext80_mul(
        ext80_add(ext80_from_f64((double)neu), ext80_from_f64((double)old)),
        ext80_from_f64(0.25));
    return ext80_to_f32(ext80_sub(left, right));
}

float x87_env(float env, float x, float a)
{
    Ext80 one_m = ext80_sub(ext80_from_f64(1.0), ext80_from_f64((double)a));
    Ext80 xx = ext80_mul(ext80_from_f64((double)x), ext80_from_f64((double)x));
    Ext80 t = ext80_mul(xx, one_m);
    Ext80 e = ext80_mul(ext80_from_f64((double)env), ext80_from_f64((double)a));
    return ext80_to_f32(ext80_add(t, e));
}

float x87_midside(float h1, float h3, float g)
{
    float side = x87_mul(x87_sub(h1, h3), g);
    Ext80 out = ext80_add(ext80_add(ext80_from_f64((double)h1), ext80_from_f64((double)h3)),
                      ext80_from_f64((double)side));
    return ext80_to_f32(out);
}
