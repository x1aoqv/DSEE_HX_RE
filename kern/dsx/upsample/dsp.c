#include "ext80.h"

#include <string.h>

#define EXPORT __declspec(dllexport)

typedef struct {
    int m, u0, u1, pair;
} Item;

typedef struct {
    Item it[96];
    int n;
} Phase;

static Phase g_plan[17][17];
static int g_ready[17][17];

static void build_plan(int S, int N, Phase *ph)
{
    int p, u, m, a, b;
    for (p = 0; p < S; p++) {
        int us[256][4];
        int nu[256];
        memset(nu, 0, sizeof nu);
        for (u = -N; u <= N; u++) {
            int mm = p - S * u;
            if (mm < 0)
                mm = -mm;
            if (mm > S * N || mm >= 256)
                continue;
            if (nu[mm] < 4)
                us[mm][nu[mm]++] = u;
        }
        ph[p].n = 0;
        for (m = 0; m <= S * N; m++) {
            int n = nu[m];
            int tmp[4];
            if (n <= 0)
                continue;
            for (a = 0; a < n; a++)
                tmp[a] = us[m][a];
            for (a = 0; a < n; a++) {
                for (b = a + 1; b < n; b++) {
                    int aa = tmp[a] < 0 ? -tmp[a] : tmp[a];
                    int bb = tmp[b] < 0 ? -tmp[b] : tmp[b];
                    if (bb < aa || (bb == aa && tmp[b] < tmp[a])) {
                        int s = tmp[a];
                        tmp[a] = tmp[b];
                        tmp[b] = s;
                    }
                }
            }
            a = 0;
            while (a + 1 < n && ph[p].n < 96) {
                Item *it = &ph[p].it[ph[p].n++];
                it->m = m;
                it->u0 = tmp[a];
                it->u1 = tmp[a + 1];
                it->pair = 1;
                a += 2;
            }
            if (a < n && ph[p].n < 96) {
                Item *it = &ph[p].it[ph[p].n++];
                it->m = m;
                it->u0 = tmp[a];
                it->pair = 0;
            }
        }
    }
}

static const Phase *plan_for(int S, int N)
{
    if (!g_ready[S][N]) {
        build_plan(S, N, g_plan[S]);
        g_ready[S][N] = 1;
    }
    return g_plan[S];
}

static void up_low(const float *h, int N, int S, const float *x, float *y,
                   int a5, int niter)
{
    const Phase *ph = plan_for(S, N);
    Ext80 ag = ext80_from_int(a5);
    Ext80 gs = ext80_from_int(S);
    int t, p, q;
    for (t = 0; t < niter; t++) {
        int k = N + t;
        for (p = 0; p < S; p++) {
            float acc = 0.f;
            int first = 1;
            q = 0;
            while (q < ph[p].n) {
                const Item *it = &ph[p].it[q];
                Ext80 v;
                int step = 1;
                if (it->pair) {
                    Ext80 s = ext80_add(ext80_from_f64(x[k + it->u0]),
                                    ext80_from_f64(x[k + it->u1]));
                    v = ext80_mul(s, ext80_from_f64(h[it->m]));
                } else {
                    v = ext80_mul(ext80_from_f64(x[k + it->u0]),
                                ext80_from_f64(h[it->m]));
                    if (q + 1 < ph[p].n && !ph[p].it[q + 1].pair) {
                        const Item *it2 = &ph[p].it[q + 1];
                        Ext80 v2 = ext80_mul(ext80_from_f64(x[k + it2->u0]),
                                         ext80_from_f64(h[it2->m]));
                        v = ext80_add(v, v2);
                        step = 2;
                    }
                }
                if (first) {
                    acc = ext80_to_f32(v);
                    first = 0;
                } else {
                    acc = ext80_to_f32(ext80_add(v, ext80_from_f64(acc)));
                }
                q += step;
            }
            {
                int i = t * S + p;
                Ext80 out = ext80_add(ext80_mul(ag, ext80_from_f64(y[i])),
                                  ext80_mul(gs, ext80_from_f64(acc)));
                y[i] = ext80_to_f32(out);
            }
        }
    }
}

static void up_high(const float *h, int N, int S, const float *x, float *y,
                    int a5, int niter)
{
    Ext80 ag = ext80_from_int(a5);
    Ext80 gs = ext80_from_int(S);
    int t, r, q;
    for (t = 0; t < niter; t++) {
        int k = N + t;
        float acc[16];
        Ext80 xa = ext80_from_f64(x[k]);
        Ext80 xb = ext80_from_f64(x[k + 1]);
        for (r = 0; r < S; r++)
            acc[r] = ext80_to_f32(ext80_mul(ext80_from_f64(h[r]), xa));
        for (r = S - 1; r >= 0; r--) {
            Ext80 term = ext80_mul(ext80_from_f64(h[S - r]), xb);
            acc[r] = ext80_to_f32(ext80_add(term, ext80_from_f64(acc[r])));
        }
        for (q = 1; q < N; q++) {
            Ext80 aq = ext80_from_f64(x[k - q]);
            Ext80 bq = ext80_from_f64(x[k + 1 + q]);
            for (r = 0; r < S; r++) {
                Ext80 term = ext80_mul(ext80_from_f64(h[S * q + r]), aq);
                acc[r] = ext80_to_f32(ext80_add(term, ext80_from_f64(acc[r])));
            }
            for (r = S - 1; r >= 0; r--) {
                Ext80 term = ext80_mul(ext80_from_f64(h[S * q + S - r]), bq);
                acc[r] = ext80_to_f32(ext80_add(term, ext80_from_f64(acc[r])));
            }
        }
        {
            Ext80 term = ext80_mul(ext80_from_f64(h[S * N]), ext80_from_f64(x[k - N]));
            acc[0] = ext80_to_f32(ext80_add(term, ext80_from_f64(acc[0])));
        }
        for (r = 0; r < S; r++) {
            int i = t * S + r;
            Ext80 out = ext80_add(ext80_mul(ag, ext80_from_f64(y[i])),
                              ext80_mul(gs, ext80_from_f64(acc[r])));
            y[i] = ext80_to_f32(out);
        }
    }
}

EXPORT void dsee_upsample(const float *h, int N, int S, const float *x,
                          float *y, int a5, int niter)
{
    if (S != 2 && S != 4 && S != 8 && S != 16)
        return;
    if (niter <= 0)
        niter = (S <= 4) ? 128 : 32;
    if (S > 4)
        up_high(h, N, S, x, y, a5, niter);
    else
        up_low(h, N, S, x, y, a5, niter);
}

EXPORT void dsee_biquad(const float *coef, float gain, float *buf,
                        const float *src, float *st, int n)
{
    int i;
    float w1, w2;
    double c0, c1, c2, c3;
    for (i = 0; i < n; i++)
        buf[i] = (float)((double)src[i] * (double)gain);
    c0 = coef[0];
    c1 = coef[1];
    c2 = coef[2];
    c3 = coef[3];
    w1 = st[0];
    w2 = st[1];
    for (i = 0; i < n; i++) {
        Ext80 w = ext80_sub(
            ext80_sub(ext80_from_f64(buf[i]),
                    ext80_mul(ext80_from_f64(c2), ext80_from_f64(w1))),
            ext80_mul(ext80_from_f64(c3), ext80_from_f64(w2)));
        float wf = ext80_to_f32(w);
        Ext80 y;
        if ((double)wf > -1.0e-40 && (double)wf < 1.0e-40)
            wf = 0.f;
        y = ext80_add(
            ext80_add(ext80_mul(ext80_from_f64(c0), ext80_from_f64(w1)),
                    ext80_from_f64(wf)),
            ext80_mul(ext80_from_f64(c1), ext80_from_f64(w2)));
        buf[i] = ext80_to_f32(y);
        w2 = w1;
        w1 = wf;
    }
    st[0] = w1;
    st[1] = w2;
}

EXPORT void dsee_fir21(const float *c, int stride, const float *in, float *out)
{
    int n, k, t;
    if (stride <= 0)
        stride = 1;
    n = 128 / stride;
    for (k = 0; k < n; k++) {
        int r = k * stride + 10;
        float acc = ext80_to_f32(ext80_mul(ext80_from_f64(in[r]), ext80_from_f64(c[0])));
        for (t = 1; t < 11; t++) {
            Ext80 pair = ext80_add(ext80_from_f64(in[r - t]), ext80_from_f64(in[r + t]));
            Ext80 term = ext80_mul(pair, ext80_from_f64(c[t]));
            acc = ext80_to_f32(ext80_add(term, ext80_from_f64(acc)));
        }
        out[k] = acc;
    }
}
