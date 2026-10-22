#include "band_frame.h"
#include "ext80.h"
#include "mem_io.h"

void dsx_biquad_cascade(uint8_t *mem, uint32_t block, float *src, float *dst,
                   float *state)
{
    float *cp = dsx_F(mem, *(uint32_t *)(mem + block));
    float gain = *(float *)(mem + block + 4);
    int n = (int)(*(uint32_t *)(mem + block + 12) << 7);
    int secs = (int)(*(uint32_t *)(mem + block + 8) / 4);
    int s, i;
    for (i = 0; i < n; i++)
        dst[i] = x87_mul(src[i], gain);
    for (s = 0; s < secs; s++) {
        float c0 = cp[4 * s + 0], c1 = cp[4 * s + 1];
        float c2 = cp[4 * s + 2], c3 = cp[4 * s + 3];
        float x1 = state[2 * s];
        float x2 = state[2 * s + 1];
        for (i = 0; i < n; i++) {

            Ext80 w = ext80_sub(
                ext80_sub(ext80_from_f64((double)dst[i]),
                        ext80_mul(ext80_from_f64((double)c2), ext80_from_f64((double)x1))),
                ext80_mul(ext80_from_f64((double)c3), ext80_from_f64((double)x2)));
            float y = ext80_to_f32(w);
            if ((double)y > -1.0e-40 && (double)y < 1.0e-40)
                y = 0.f;
            {
                Ext80 out = ext80_add(
                    ext80_add(ext80_mul(ext80_from_f64((double)c0), ext80_from_f64((double)x1)),
                            ext80_from_f64((double)y)),
                    ext80_mul(ext80_from_f64((double)c1), ext80_from_f64((double)x2)));
                dst[i] = ext80_to_f32(out);
            }
            x2 = x1;
            x1 = y;
        }
        state[2 * s] = x1;
        state[2 * s + 1] = x2;
    }
}
