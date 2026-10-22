#include "band_frame.h"
#include "mem_io.h"

void dsx_energy_tap22(uint8_t *mem, uint32_t coef, uint32_t buf, uint32_t out,
                   int nout)
{
    float *c = dsx_F(mem, coef);
    float *b = (float *)(mem + buf + 68);
    float e0 = 0.f, e1 = 0.f, e2 = 0.f, e3 = 0.f, es = 0.f;
    int r;
    for (r = 0; r < 16; r++) {
        float *v4 = c + 4;
        float *v5 = b + 4;
        float *v6 = b;
        float v8 = b[3];
        float a0 = x87_mul(c[0], v8);
        float a1 = x87_mul(c[1], v8);
        float a2 = x87_mul(c[2], v8);
        float a3 = x87_mul(v8, c[3]);
        int k;
        for (k = 0; k < 2; k++) {
            float v12, v13, v14, v15, v16, v17, v18, v19, v20, v21;
            v12 = x87_add(v6[2], v5[0]);
            a0 = x87_mac(a0, v4[0], v12);
            a1 = x87_mac(a1, v4[1], v12);
            a2 = x87_mac(a2, v4[2], v12);
            a3 = x87_mac(a3, v4[3], v12);
            v13 = x87_add(v5[1], v6[1]);
            a0 = x87_mac(a0, v4[4], v13);
            a1 = x87_mac(a1, v4[5], v13);
            a2 = x87_mac(a2, v4[6], v13);
            a3 = x87_mac(a3, v4[7], v13);
            v14 = x87_add(v5[2], v6[0]);
            a0 = x87_mac(a0, v4[8], v14);
            a1 = x87_mac(a1, v4[9], v14);
            a2 = x87_mac(a2, v4[10], v14);
            a3 = x87_mac(a3, v4[11], v14);
            v15 = x87_add(v5[3], v6[-1]);
            a0 = x87_mac(a0, v4[12], v15);
            a1 = x87_mac(a1, v4[13], v15);
            a2 = x87_mac(a2, v4[14], v15);
            a3 = x87_mac(a3, v4[15], v15);
            v16 = x87_add(v5[4], v6[-2]);
            a0 = x87_mac(a0, v4[16], v16);
            a1 = x87_mac(a1, v4[17], v16);
            a2 = x87_mac(a2, v4[18], v16);
            a3 = x87_mac(a3, v4[19], v16);
            v17 = x87_add(v5[5], v6[-3]);
            a0 = x87_mac(a0, v4[20], v17);
            a1 = x87_mac(a1, v4[21], v17);
            a2 = x87_mac(a2, v4[22], v17);
            a3 = x87_mac(a3, v4[23], v17);
            v18 = x87_add(v5[6], v6[-4]);
            a0 = x87_mac(a0, v4[24], v18);
            a1 = x87_mac(a1, v4[25], v18);
            a2 = x87_mac(a2, v4[26], v18);
            a3 = x87_mac(a3, v4[27], v18);
            v19 = x87_add(v5[7], v6[-5]);
            v5 += 10;
            a0 = x87_mac(a0, v4[28], v19);
            v6 -= 10;
            v4 += 40;
            a1 = x87_mac(a1, v4[-11], v19);
            a2 = x87_mac(a2, v4[-10], v19);
            a3 = x87_mac(a3, v4[-9], v19);
            v20 = x87_add(v5[-2], v6[4]);
            a0 = x87_mac(a0, v4[-8], v20);
            a1 = x87_mac(a1, v4[-7], v20);
            a2 = x87_mac(a2, v4[-6], v20);
            a3 = x87_mac(a3, v4[-5], v20);
            v21 = x87_add(v5[-1], v6[3]);
            a0 = x87_mac(a0, v4[-4], v21);
            a1 = x87_mac(a1, v4[-3], v21);
            a2 = x87_mac(a2, v4[-2], v21);
            a3 = x87_mac(a3, v4[-1], v21);
        }
        b += 8;
        e0 = x87_sqadd(e0, a0);
        e1 = x87_sqadd(e1, a1);
        e2 = x87_sqadd(e2, a2);
        e3 = x87_sqadd(e3, a3);
        if (nout > 4) {

            float s = x87_add(x87_add(x87_add(a0, a1), a2), a3);
            es = x87_sqadd(es, s);
        }
    }
    dsx_F(mem, out)[0] = x87_mul(e0, 0.5f);
    dsx_F(mem, out)[1] = x87_mul(e1, 0.5f);
    dsx_F(mem, out)[2] = x87_mul(e2, 0.5f);
    dsx_F(mem, out)[3] = x87_mul(e3, 0.5f);
    if (nout > 4)
        dsx_F(mem, out)[4] = x87_mul(es, 0.5f);
}
