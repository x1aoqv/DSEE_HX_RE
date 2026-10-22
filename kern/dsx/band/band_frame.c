#include "band_frame.h"
#include "dsx.h"
#include "mem_io.h"

__declspec(dllexport) void dsee_f76_frame(uint8_t *mem, float *pcm, int a3, int a4, int a5)
{
    uint8_t *c = mem + 84;
    int nslots = (int)*(uint32_t *)(mem + 4);
    int ch;

    *(uint32_t *)(c + 4440) = *(uint32_t *)(mem + 8);

    for (ch = 0; ch < nslots; ch++) {
        uint32_t u = (uint32_t)((c - mem) + 944 + ch * DSX_SLOT);
        dsx_gather(pcm, dsx_F(mem, *(uint32_t *)(mem + u)), a3, ch, nslots);
        dsx_clear_f(dsx_F(mem, *(uint32_t *)(mem + u) + 4 * a3), 128 - a3);
    }

    if (*(uint32_t *)(c + 19324) != 2) {
        for (ch = 0; ch < nslots; ch++) {
            uint32_t u = (uint32_t)((c - mem) + 944 + ch * DSX_SLOT);
            dsx_biquad_cascade(mem, (uint32_t)((c - mem) + 4344),
                   dsx_F(mem, *(uint32_t *)(mem + u)),
                   dsx_F(mem, *(uint32_t *)(mem + u + 4) + 832),
                   dsx_F(mem, u - 880));
            dsx_energy_tap22(mem, *(uint32_t *)(mem + (c - mem) + 4384),
                   *(uint32_t *)(mem + u + 4) + 672, u - 48, 4);
        }

        for (ch = 0; ch < nslots; ch++) {
            uint32_t u = (uint32_t)((c - mem) + 944 + ch * DSX_SLOT);
            dsx_biquad_cascade(mem, (uint32_t)((c - mem) + 4328),
                   dsx_F(mem, *(uint32_t *)(mem + u)),
                   dsx_F(mem, u - 736), dsx_F(mem, u - 944));
        }

        {
            uint32_t b1 = (uint32_t)((c - mem) + 208);
            uint32_t b2 = (nslots == 1) ? b1 : (uint32_t)((c - mem) + 2372);
            uint32_t arr[2];
            uint32_t arr2[2];
            int i;
            for (i = 0; i < nslots && i < 2; i++) {
                arr[i] = (i == 0) ? b1 : b2;
                arr2[i] = (i == 0) ? b1 : b2;
            }
            dsx_envelope_ms(mem, nslots, a3, (uint32_t)((c - mem) + 18660),
                (uint32_t)((c - mem) + 18744),
                *(float *)(mem + (c - mem) + 19012), arr, arr2);
        }

        for (ch = 0; ch < nslots; ch++) {
            uint32_t u2 = (uint32_t)((c - mem) + 948 + ch * DSX_SLOT);
            dsx_biquad_cascade(mem, (uint32_t)((c - mem) + 4360),
                   dsx_F(mem, u2 - 740),
                   dsx_F(mem, *(uint32_t *)(mem + u2) + 160), dsx_F(mem, u2 - 852));
        }

        for (ch = 0; ch < nslots; ch++) {
            uint32_t u3 = (uint32_t)((c - mem) + ch * DSX_SLOT);
            uint32_t v22 = u3 + 856;
            int n;
            for (n = 0; n < 7; n++) {
                dsx_copy_f(dsx_F(mem, v22 + 20), dsx_F(mem, v22), 5);
                v22 -= 20;
            }
            dsx_energy_tap22(mem, *(uint32_t *)(mem + (c - mem) + 4376),
                   *(uint32_t *)(mem + u3 + 948), u3 + 736, 5);
        }

        for (ch = 0; ch < nslots; ch++) {
            uint32_t u4 = (uint32_t)((c - mem) + 948 + ch * DSX_SLOT);
            dsx_band_gain8(mem, (uint32_t)((c - mem) + ch * DSX_SLOT),
                 (uint32_t)((c - mem) + 4388),
                 (uint32_t)((c - mem) + 4376), (int)*(uint32_t *)(mem + (c - mem) + 4444),
                 (uint32_t)(c - mem));
            dsx_synth_fir22(mem, *(uint32_t *)(mem + (c - mem) + 4380),
                  (uint32_t)((c - mem) + 4388),
                  *(uint32_t *)(mem + u4), *(uint32_t *)(mem + u4 - 4));
        }

    }

    for (ch = 0; ch < nslots; ch++) {
        uint32_t u5 = (uint32_t)((c - mem) + 944 + ch * DSX_SLOT);
        dsx_biquad_cascade(mem, (uint32_t)((c - mem) + 4344),
               dsx_F(mem, *(uint32_t *)(mem + u5)),
               dsx_F(mem, *(uint32_t *)(mem + u5)), dsx_F(mem, u5 - 912));
    }

    for (ch = 0; ch < nslots; ch++) {
        uint32_t v28 = (uint32_t)((c - mem) + 108 + ch * DSX_SLOT);
        uint32_t buf = *(uint32_t *)(mem + v28 + 836);
        int i;
        if (*(uint32_t *)(c + 19324) == 1) {
            float *d = dsx_F(mem, buf + 8);
            float *s = dsx_F(mem, v28);
            for (i = 0; i < 16; i++) {
                d[8 * i - 2] = s[8 * i - 1];
                d[8 * i - 1] = s[8 * i + 0];
                d[8 * i + 0] = s[8 * i + 1];
                d[8 * i + 1] = s[8 * i + 2];
                d[8 * i + 2] = s[8 * i + 3];
                d[8 * i + 3] = s[8 * i + 4];
                d[8 * i + 4] = s[8 * i + 5];
                d[8 * i + 5] = s[8 * i + 6];
            }
        } else if (*(uint32_t *)(c + 19324) != 2) {
            float *d = dsx_F(mem, buf + 8);
            float *s = dsx_F(mem, v28);
            for (i = 0; i < 16; i++) {
                d[8 * i - 2] = x87_add(s[8 * i - 1], d[8 * i - 2]);
                d[8 * i - 1] = x87_add(s[8 * i + 0], d[8 * i - 1]);
                d[8 * i + 0] = x87_add(s[8 * i + 1], d[8 * i + 0]);
                d[8 * i + 1] = x87_add(s[8 * i + 2], d[8 * i + 1]);
                d[8 * i + 2] = x87_add(s[8 * i + 3], d[8 * i + 2]);
                d[8 * i + 3] = x87_add(s[8 * i + 4], d[8 * i + 3]);
                d[8 * i + 4] = x87_add(s[8 * i + 5], d[8 * i + 4]);
                d[8 * i + 5] = x87_add(s[8 * i + 6], d[8 * i + 5]);
            }
        }

        dsx_writeback(dsx_F(mem, buf + 4 * a5), pcm, a4, (int)*(uint32_t *)(mem + 32),
                  ch, nslots);

        dsx_copy_f(dsx_F(mem, v28 - 4), dsx_F(mem, v28 + 508), 26);
        {
            uint32_t b2 = *(uint32_t *)(mem + v28 + 840);
            dsx_copy_f(dsx_F(mem, b2), dsx_F(mem, b2 + 512), 40);
            dsx_copy_f(dsx_F(mem, b2 + 672), dsx_F(mem, b2 + 1184), 40);
        }
    }
}
