#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

#define INNER 84
#define SLOT 2164
#define EXT_OFF 0x20000
#define SNAP_RVA 0x1ADD8
#define SNAP_END 0x1C400
#define TBL_RVA0 0x1A8E8
#define MEM_CTX (EXT_OFF + (SNAP_END - SNAP_RVA) + 64)

extern const uint32_t dsee_seed[128];
extern const float dsee_scale[256];
extern const float dsee_bf[256];
extern const float dsee_up_44100_2x[34];
extern const float dsee_up_48000_2x[34];
extern const float dsee_up_44100_4x[50];
extern const float dsee_up_48000_4x[49];
extern const float dsee_biq[5];
extern const float dsee_common[44];
extern const float dsee_an_44100_2x[158];
extern const float dsee_an_48000_2x[158];
extern const float dsee_an_44100_4x[238];
extern const float dsee_an_48000_4x[238];
extern const float dsee_sh[410];
extern const float dsee_f76_coeff[316];

static const int RATE_TAB[16] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100,
    48000, 64000, 88200, 96000, 128000, 144000, 176400, 192000,
};

typedef struct {
    int f76, f80, upmode, src, dst, step_src, step_dst, n12, f18992, nblk;
    int off_up, off_low, channels, nch_gate, st1, active, rs_in, rs_out;
    int rate;
    float gain_a, gain_b, gain_c, gain_e;
} DseeCfg;

typedef struct {
    int mode;
    int channels;
    int codec;
    int input_fs;
    int output_fs;
    int input_bits;
    int output_bits;
    int vbr;
    int abr;
    int sb;
    int head;
    int dcr;
    int gain;
    int offset;
} DseeSource;

int dsee_pick_level(int channels, int vbr, int content_hz, int src_hz,
                    int codec, unsigned energy, int bit_override, int *level);

static uint32_t ru32(const uint8_t *b, int off)
{
    uint32_t v;
    memcpy(&v, b + off, 4);
    return v;
}
static void wu32(uint8_t *b, int off, uint32_t v) { memcpy(b + off, &v, 4); }
static float rf32(const uint8_t *b, int off)
{
    float v;
    memcpy(&v, b + off, 4);
    return v;
}
static void wf32(uint8_t *b, int off, float v) { memcpy(b + off, &v, 4); }
static void zero_f(uint8_t *b, int off, int n)
{
    if (n > 0)
        memset(b + off, 0, (size_t)n * 4);
}

typedef struct {
    uint8_t *mem;
    int cur;
    uint32_t acc;
    int cnt;
    const float *scale;
} BS;

static int bs_read(BS *b, int n, int sgn)
{
    uint32_t result = 0;
    int shift = 0, i;
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (b->cnt == 4) {
            b->acc = ru32(b->mem, b->cur);
            b->cur += 4;
            b->cnt = 0;
        }
        b->cnt++;
        result |= (uint32_t)(b->acc & 0xFFu) << shift;
        b->acc = (uint32_t)((int32_t)b->acc >> 8);
        shift += 8;
    }
    if (sgn) {
        int32_t v = (int32_t)(result << (32 - 8 * n));
        v >>= (32 - 8 * n);
        return (int)v;
    }
    return (int)result;
}

static float bs_scaled(BS *b, int n, int sgn)
{
    int idx = bs_read(b, 1, 0);
    int v = bs_read(b, n, sgn);
    return (float)((float)v * b->scale[idx & 255]);
}

static void bs_block(BS *b, int a2, int a3, int dst_off)
{
    int v5 = a2 + 1;
    int codes[16];
    int ncode = 0, i, k;
    if (a2 != -1) {
        if (v5 > 16)
            v5 = 16;
        for (i = 0; i < v5; i++)
            codes[ncode++] = bs_read(b, 1, 0);
    }
    if (a3 == 0)
        return;
    for (i = 0; i < a3; i++) {
        for (k = 0; k < ncode; k++) {
            int v = bs_read(b, 1, 1);
            wf32(b->mem, dst_off, (float)((float)v * b->scale[codes[k] & 255]));
            dst_off += 4;
        }
    }
}

static void xor_fill(uint8_t *mem, int off, const uint8_t *seed)
{
    uint32_t s = 0x163FBu;
    int i;
    for (i = 0; i < 128; i++) {
        uint32_t w, v, m;
        memcpy(&w, seed + 4 * i, 4);
        v = 1103515245u * s + 12345u;
        s = v;
        m = ((v >> 16) & 0x7FFFu) >> 7;
        m &= 0xFFu;
        m |= m << 8;
        m |= m << 16;
        wu32(mem, off + 4 * i, w ^ m);
    }
}

static int rate_index(int hz)
{
    int i;
    for (i = 0; i < 16; i++)
        if (RATE_TAB[i] == hz)
            return i;
    return -1;
}

static int check_params(uint8_t *hdr, int ch, int mode, int src, int dst,
                        int codec, int cfs, int vbr, int abr, int sb,
                        int head, int dcr, int gain, int offset)
{
    int is, id, ic;
    if (ch < 1 || ch > 2 || codec > 0x11)
        return 30;
    if (rate_index(cfs) < 0 || vbr > 1 || abr > 0x3FC || mode > 3)
        return 30;
    if (sb != 0 && (sb < 8 || sb > 16))
        return 30;
    if (head > 0x7D0 || dcr > 1 || gain > 0x96 || offset < -601 || offset > 60)
        return 30;

    if (src != 44100 && src != 48000 && src != 88200 && src != 96000 &&
        src != 176400 && src != 192000)
        return 30;
    if (dst != 44100 && dst != 48000 && dst != 88200 && dst != 96000 &&
        dst != 176400 && dst != 192000)
        return 30;

    if (dst != src && dst != 2 * src && dst != 4 * src)
        return 30;
    memset(hdr, 0, 32);
    is = rate_index(src);
    id = rate_index(dst);
    ic = rate_index(cfs);
    hdr[0] = 2;
    hdr[1] = (uint8_t)(((is & 0x0F) << 4) | (ch & 0x0F));
    hdr[2] = (uint8_t)(id & 0xFF);
    hdr[3] = (uint8_t)(((vbr & 1) << 4) | (ic & 0x0F));
    hdr[4] = (uint8_t)((abr >> 2) & 0xFF);
    hdr[5] = (uint8_t)(((dcr & 0x0F) << 4) | (mode & 0x0F));
    hdr[6] = (uint8_t)(codec & 0xFF);
    wu32(hdr, 8, (uint32_t)sb);
    wu32(hdr, 12, (uint32_t)head);
    wu32(hdr, 16, (uint32_t)gain);
    wu32(hdr, 20, (uint32_t)offset);
    return 0;
}

static void parse_desc(const uint8_t *d, int *st)
{
    st[0] = d[1] & 0x0F;
    st[4] = d[1] >> 4;
    st[5] = d[2] & 0x0F;
    st[3] = d[3] & 0x0F;
    st[6] = d[3] >> 4;
    st[8] = 4 * d[4];
    st[9] = d[5] & 0x0F;
    st[12] = d[5] >> 4;
    st[2] = d[6];
    memcpy(&st[10], d + 8, 4);
    memcpy(&st[11], d + 12, 4);
    memcpy(&st[13], d + 16, 4);
    memcpy(&st[14], d + 20, 4);
    st[1] = st[9] ? 10 : 0;
    st[7] = 4;
}

static int decide(const int *st, int f76_init, DseeCfg *cfg)
{
    int v2 = RATE_TAB[st[4] & 15];
    int v3 = RATE_TAB[st[5] & 15];
    int v10 = RATE_TAB[st[3] & 15];
    int v5, f76, f80, mode = st[9], f19076, v6, hit = 0, f18988 = 0, f18992;
    int step_src, step_dst, off_up;
    float tenth = 0.10000000149011612f;
    float gain_c, gain_e;
    if (v3 != v2 && v3 != 2 * v2 && v3 != 4 * v2)
        return 1;
    if (v2 == 44100 || v2 == 88200 || v2 == 176400)
        v5 = 44100;
    else if (v2 == 48000 || v2 == 96000 || v2 == 192000)
        v5 = 48000;
    else
        return 2;
    f76 = f76_init;
    f80 = 1;
    if (mode == 0) {
        f76 = 0;
        f80 = 0;
    } else if (mode == 1) {
        if (v2 != v5 || v3 != v5)
            return 3;
        f80 = 0;
    } else if (mode == 2) {
        f76 = 0;
    } else if (mode != 3) {
        return 4;
    }
    if ((v2 == 2 * v5 || v2 == 4 * v5) && v10 > v5)
        f76 = 0;
    if (v3 == v5 || (v2 > v5 && v10 > v5 &&
                     (v3 != 4 * v5 || v5 >= v10 || v10 > 2 * v5)))
        f80 = 0;
    if (st[14] == -601) {
        f76 = 0;
        f80 = 0;
    }
    f19076 = (f76 == 0 && f80 == 1 && v2 == v3) ? 1 : 0;
    v6 = v3 / v2;
    if (f80 == 1) {
        if (v2 == 2 * v5 && v3 == 2 * v5) {
            v6 = 2; f18988 = 14; hit = 1;
        } else if (v2 == 2 * v5 && v3 == 4 * v5 && v10 <= v5) {
            v6 = 4; f18988 = 12; hit = 1;
        } else if (v2 == 4 * v5 && v3 == 4 * v5 && v5 >= v10) {
            v6 = 4; f18988 = 12; hit = 1;
        } else if (v2 == 4 * v5 && v3 == 4 * v5 && v10 <= 2 * v5) {
            v6 = 2; f18988 = 14; hit = 1;
        }
    }
    if (!hit) {
        if (v6 == 2) {
            f18988 = 14;
            hit = 1;
        } else if (v6 == 4) {
            f18988 = 12;
            hit = 1;
        }
    }
    f18992 = hit ? 2 : 0;
    if (v2 > 96000)
        step_src = 512;
    else if (v2 == 44100 || v2 == 48000)
        step_src = 128;
    else
        step_src = 256;
    if (v3 > 96000)
        step_dst = 512;
    else if (v3 == 44100 || v3 == 48000)
        step_dst = 128;
    else if (v3 == 88200 || v3 == 96000)
        step_dst = 256;
    else
        return 5;
    if (v6 == 2)
        off_up = f19076 ? 134 : 102;
    else if (v6 == 4)
        off_up = f19076 ? 236 : 188;
    else
        off_up = 0;
    gain_c = tenth * (float)st[14];
    if (f76 == 1)
        gain_e = gain_c;
    else if (f80 == 1)
        gain_e = (float)pow(10.0, (double)gain_c / 20.0);
    else
        gain_e = 1.0f;
    cfg->f76 = f76;
    cfg->f80 = f80;
    cfg->upmode = v6;
    cfg->rate = v5;
    cfg->src = v2;
    cfg->dst = v3;
    cfg->step_src = step_src;
    cfg->step_dst = step_dst;
    cfg->off_up = off_up;
    cfg->off_low = 0;
    cfg->n12 = f18988;
    cfg->f18992 = f18992;
    cfg->nblk = f19076;
    cfg->gain_a = tenth * (float)st[11];
    cfg->gain_b = tenth * (float)st[13];
    cfg->gain_c = gain_c;
    cfg->gain_e = gain_e;
    cfg->st1 = st[1];
    cfg->channels = st[0];
    cfg->nch_gate = st[7];
    cfg->active = (f80 || f76) ? 1 : 0;
    cfg->rs_in = step_src * (512 / step_dst);
    cfg->rs_out = 512;
    return 0;
}

static int pick_f76(int *st)
{
    int level = 16;
    int f76;
    st[15] = 16;
    if (st[1] == 0)
        return 0;
    f76 = dsee_pick_level(st[0], st[6], RATE_TAB[st[3] & 15],
                          RATE_TAB[st[4] & 15], st[2], (unsigned)st[8],
                          st[10], &level);
    st[15] = level;
    return f76;
}

static void alloc_buffers(uint8_t *mem)
{
    int v2 = 5123 & ~15;
    static const int seq[5] = {512, 1344, 512, 1344, 336};
    static const int idx[5] = {257, 258, 798, 799, 1115};
    int i;
    for (i = 0; i < 5; i++) {
        wu32(mem, idx[i] * 4, (uint32_t)v2);
        v2 += seq[i];
    }
    wu32(mem, 1116 * 4, (uint32_t)v2);
    wu32(mem, 1117 * 4, (uint32_t)(v2 + 168));
    {
        int v3 = 9691 & ~15;
        static const int seq3[6] = {2992, 1232, 44, 2992, 1232, 44};
        static const int idx3[6] = {259, 260, 261, 800, 801, 802};
        for (i = 0; i < 6; i++) {
            wu32(mem, idx3[i] * 4, (uint32_t)v3);
            v3 += seq3[i];
        }
        wu32(mem, 4751 * 4, (uint32_t)v3);
    }
}

static void config_tables(uint8_t *mem)
{
    int c = INNER;
    int rate = (int)ru32(mem, c + 18888);
    int up = (int)ru32(mem, c + 18892);
    if (up == 4) {
        int rva = rate == 44100 ? 0x1AEE8 : 0x1AFB0;
        wu32(mem, c + 18936, (uint32_t)(EXT_OFF + (rva - SNAP_RVA)));
        wu32(mem, c + 18940, 12);
        wu32(mem, c + 18964, 12);
        wu32(mem, c + 18960, (uint32_t)(c + 1392));
        wu32(mem, c + 18972, 12);
        wu32(mem, c + 18980, 2);
        wu32(mem, c + 18976, (uint32_t)(EXT_OFF + (0x1C100 - SNAP_RVA)));
        wu32(mem, c + 18968, (uint32_t)(c + 3556));
        wu32(mem, c + 18988, 2);
        wu32(mem, c + 18984, (uint32_t)(EXT_OFF + (0x1C100 - SNAP_RVA)));
        wu32(mem, c + 18928,
             (uint32_t)(EXT_OFF + ((rate == 44100 ? 0x1B628 : 0x1B9E0) - SNAP_RVA)));
        wu32(mem, c + 18932, 164);
        wu32(mem, c + 18924, (uint32_t)(EXT_OFF + (0x1BF28 - SNAP_RVA)));
    } else if (up == 2) {
        int rva = rate == 44100 ? 0x1ADD8 : 0x1AE60;
        wu32(mem, c + 18936, (uint32_t)(EXT_OFF + (rva - SNAP_RVA)));
        wu32(mem, c + 18940, 16);
        wu32(mem, c + 18964, 14);
        wu32(mem, c + 18960, (uint32_t)(c + 1392));
        wu32(mem, c + 18972, 14);
        wu32(mem, c + 18980, 2);
        wu32(mem, c + 18976, (uint32_t)(EXT_OFF + (0x1C0B8 - SNAP_RVA)));
        wu32(mem, c + 18968, (uint32_t)(c + 3556));
        wu32(mem, c + 18988, 2);
        wu32(mem, c + 18984, (uint32_t)(EXT_OFF + (0x1C0B8 - SNAP_RVA)));
        wu32(mem, c + 18928,
             (uint32_t)(EXT_OFF + ((rate == 44100 ? 0x1B138 : 0x1B3B0) - SNAP_RVA)));
        wu32(mem, c + 18932, 84);
        wu32(mem, c + 18924, (uint32_t)(EXT_OFF + (0x1BD98 - SNAP_RVA)));
    }
    wu32(mem, c + 18920, (uint32_t)(EXT_OFF + (0x1B088 - SNAP_RVA)));
    wu32(mem, c + 18944, (uint32_t)(EXT_OFF + (0x1B074 - SNAP_RVA)));
    wf32(mem, c + 18948, 0.7690864f);
    wu32(mem, c + 18952, 4);
    wu32(mem, c + 18956, 1);
}

static float *MF(uint8_t *mem, uint32_t off)
{
    return (float *)(mem + off);
}

static void f76_meta(uint8_t *mem, int idx)
{
    int m = INNER + 4388;
    int bands = 16 - idx;
    if (bands > 8)
        bands = 8;
    wu32(mem, m + 0, (uint32_t)MEM_CTX);
    wu32(mem, m + 4, 4);
    wu32(mem, m + 8, (uint32_t)idx);
    wu32(mem, m + 12, (uint32_t)bands);
    wu32(mem, m + 16, 4);
    wu32(mem, m + 20, 0);
    wu32(mem, m + 24, 1);
    wu32(mem, m + 28, 2);
    wu32(mem, m + 32, 3);
    wu32(mem, m + 36, 0);
    wu32(mem, m + 40, 0);
    wu32(mem, m + 44, 0);
    wu32(mem, m + 48, 0);
}

static void f76_coef(uint8_t *mem, uint32_t dst_off, int idx)
{
    const double K = 6.2831854820251465 * 0.015625;
    const float *tab = dsee_f76_coeff + 294;
    float *dst = MF(mem, dst_off);
    float angle[8];
    int n = 4, i, row;

    memset(dst, 0, 84 * sizeof(float));
    for (i = 0; i < n; i++)
        angle[i] = (float)((2.0 * idx - 2.0 * n + 1.0 + 2.0 * i) * K);
    for (row = 0; row < 21; row++) {
        for (i = 0; i < n; i++) {
            double c = cos((double)angle[i] * (double)(row - 20));
            dst[80 - 4 * row + i] = (float)(c * (double)(2.0f * tab[row]));
        }
    }
}

static void f76_biquad(uint8_t *mem, int idx)
{
    int c = INNER;
    int offA = 0x1A940 + 64 * idx - 512;
    int offB = 0x1AB60 + 64 * idx - 512;
    int offG = 0x1AB20 + 4 * idx;
    int offH = 0x1AD40 + 4 * idx;
    if (offA < TBL_RVA0)
        offA = TBL_RVA0;
    if (offB < TBL_RVA0)
        offB = TBL_RVA0;
    wu32(mem, c + 4328, (uint32_t)(MEM_CTX + (offA - TBL_RVA0)));
    wf32(mem, c + 4332, rf32(mem, MEM_CTX + (offG - TBL_RVA0)));
    wu32(mem, c + 4336, 16);
    wu32(mem, c + 4340, 1);
    wu32(mem, c + 4344, (uint32_t)(MEM_CTX + (offB - TBL_RVA0)));
    wf32(mem, c + 4348, rf32(mem, MEM_CTX + (offH - TBL_RVA0)));
    wu32(mem, c + 4352, 16);
    wu32(mem, c + 4356, 1);
    wu32(mem, c + 4360, (uint32_t)(MEM_CTX + (0x1A928 - TBL_RVA0)));
    wf32(mem, c + 4364, 0.7690864f);
    wu32(mem, c + 4368, 4);
    wu32(mem, c + 4372, 1);
}

static void f76_tables(uint8_t *mem, int level)
{
    int c = INNER;
    f76_meta(mem, level + 4);
    f76_coef(mem, ru32(mem, c + 4384), level + 4);
    f76_meta(mem, level);
    f76_coef(mem, ru32(mem, c + 4376), level);
    f76_biquad(mem, level);
}

static void sidechain(BS *bs, float a3, int rate, float a5, uint8_t *mem)
{
    int base = INNER + 18660;
    float v10, v6, v8, v9;
    int i;
    bs_read(bs, 1, 0);
    wf32(mem, base, (float)rate);
    for (i = 1; i < 6; i++)
        wf32(mem, base + 4 * i, bs_scaled(bs, 1, 0));
    wf32(mem, base + 24, bs_scaled(bs, 2, 0));
    wf32(mem, base + 28, bs_scaled(bs, 2, 0));
    wf32(mem, base + 32, a3 - bs_scaled(bs, 2, 0));
    wf32(mem, base + 36, a3 - bs_scaled(bs, 1, 1));
    v10 = bs_scaled(bs, 1, 1);
    if (a5 != 0.0f)
        v10 = a5;
    wf32(mem, base + 40, v10);
    wf32(mem, base + 44, bs_scaled(bs, 1, 1));
    wf32(mem, base + 48, bs_scaled(bs, 1, 1));
    for (i = 13; i < 19; i++)
        wf32(mem, base + 4 * i, bs_scaled(bs, 1, 0));
    v6 = bs_scaled(bs, 1, 1);
    wf32(mem, base + 76, v6);
    if (-v6 > rf32(mem, base + 36))
        wf32(mem, base + 36, -v6);
    wf32(mem, base + 80, bs_scaled(bs, 1, 1));
    if (a3 == 0.0f) {
        for (i = 10; i < 14; i++)
            wf32(mem, base + 4 * i, 0.0f);
    }
    v8 = (float)pow(10.0, (double)rf32(mem, base + 36) / 20.0);
    wf32(mem, base + 60, v8);
    wf32(mem, base + 68, v8);
    v9 = (float)pow(10.0,
                    ((double)rf32(mem, base + 36) - (double)rf32(mem, base + 32)) / 20.0);
    wf32(mem, base + 64, v9);
}

static void stream_init(uint8_t *mem, const uint8_t *seed, const float *scale,
                        const float *bf)
{
    int c = INNER;
    BS bs;
    int scratch, level, j, k, i;
    float db;

    wu32(mem, c + 19304, 91131);
    xor_fill(mem, c + 4512, seed);
    bs.mem = mem;
    bs.cur = c + 4512 + 4;
    bs.acc = ru32(mem, c + 4512);
    bs.cnt = 0;
    bs.scale = scale;

    bs_block(&bs, 4, 8, c + 18436);
    bs_block(&bs, 1, 8, c + 18596);
    {
        float v44 = bs_scaled(&bs, 1, 0);
        if (rf32(mem, c + 19012) == 0.0f)
            wf32(mem, c + 19012, v44);
    }
    wf32(mem, c + 4452, (float)((float)bs_read(&bs, 1, 0) * 0.10000000149011612f));
    wu32(mem, c + 4460, (uint32_t)bs_read(&bs, 1, 0));
    {
        int v39 = bs_read(&bs, 1, 0);
        float sc = bf[v39 & 255];
        for (i = 0; i < 8; i++)
            wf32(mem, c + 4464 + 4 * i,
                 (float)((float)bs_read(&bs, 1, 0) * sc));
    }
    wu32(mem, c + 4496, (uint32_t)bs_read(&bs, 1, 0));
    {
        int v11 = bs_read(&bs, 1, 0);
        if (ru32(mem, c + 19016))
            v11 = 1;
        wu32(mem, c + 4444, (uint32_t)v11);
    }
    wf32(mem, c + 4448, (float)bs_read(&bs, 1, 0));
    if (ru32(mem, 8) == 0)
        wf32(mem, c + 4448, 0.0f);
    wu32(mem, c + 4500, (uint32_t)bs_read(&bs, 1, 0));
    sidechain(&bs, rf32(mem, c + 4448), (int)ru32(mem, c + 18888),
              rf32(mem, c + 19020), mem);
    bs_read(&bs, 4, 0);

    scratch = (int)ru32(mem, c + 956);
    level = (int)ru32(mem, 64);
    for (j = 9; j < 16; j++) {
        int second;
        if (j == level) {
            bs_block(&bs, 4, 16 - j, c + 18436);
            second = c + 18596;
        } else {
            bs_block(&bs, 4, 16 - j, scratch);
            second = scratch;
        }
        bs_block(&bs, 1, 16 - j, second);
    }
    db = rf32(mem, c + 19012);
    {
        int pA = c + 18436, pB = c + 18596;
        for (i = 0; i < 8; i++) {
            float t = rf32(mem, pA + 4);
            t = (float)(t + rf32(mem, pA + 8));
            t = (float)(t + rf32(mem, pA + 12));
            t = (float)(t + rf32(mem, pA + 16));
            wf32(mem, pA, (float)((t - 1.0f) * db + rf32(mem, pA)));
            wf32(mem, pB, (float)((rf32(mem, pB + 4) - 1.0f) * db + rf32(mem, pB)));
            pA += 20;
            pB += 8;
        }
    }
    wf32(mem, c + 4504, (float)((float)bs_read(&bs, 1, 1) * 0.10000000149011612f));
    wf32(mem, c + 4508, (float)((float)bs_read(&bs, 1, 1) * 0.10000000149011612f));
    {
        float v49 = (float)bs_read(&bs, 1, 1);
        wf32(mem, c + 19032,
             (float)(pow(10.0, (double)v49 / 20.0) * (double)rf32(mem, c + 19032)));
    }
    {
        float v21 = (float)bs_read(&bs, 1, 0);
        wf32(mem, c + 4456, (float)(v21 * 0.10000000149011612f));
    }
    {
        int flags[4] = {0, 0, 0, 0};
        int up = (int)ru32(mem, c + 18892), rate = (int)ru32(mem, c + 18888);
        if (up == 2 && rate == 44100) flags[0] = 1;
        else if (up == 2 && rate == 48000) flags[1] = 1;
        else if (up == 4 && rate == 44100) flags[2] = 1;
        else if (up == 4 && rate == 48000) flags[3] = 1;
        for (k = 0; k < 4; k++)
            bs_block(&bs, 3, 4, flags[k] ? c + 19036 : scratch);
    }
    for (i = 0; i < 16; i++)
        if (rf32(mem, c + 19036 + 4 * i) == -32.0f)
            wf32(mem, c + 19036 + 4 * i, -200.0f);
    {
        int p = c + 19036;
        for (i = 0; i < 4; i++) {
            float q = rf32(mem, p + 4);
            q = (float)(q + rf32(mem, p + 8));
            q = (float)(q + rf32(mem, p + 12));
            wf32(mem, p, (float)((q - 1.0f) * db + rf32(mem, p)));
            p += 16;
        }
    }
    {
        float v54 = (float)bs_read(&bs, 1, 1);
        if (v54 >= -120.0f) {
            float v55 = (float)pow(10.0, ((double)v54 - (double)db) / 20.0);
            wf32(mem, c + 19308, (float)(v55 * 0.000030517578125f));
        } else {
            wf32(mem, c + 19308, 0.0f);
        }
    }
    {
        float v56 = (float)((float)bs_read(&bs, 1, 1) * 0.10000000149011612f);
        if (ru32(mem, c + 18992))
            wf32(mem, c + 19036, (float)(rf32(mem, c + 19036) + v56));
    }
    wu32(mem, c + 19324, (uint32_t)bs_read(&bs, 1, 0));

    {
        int p = c + 1392;
        for (i = 0; i < 2; i++) {
            zero_f(mem, p, 193);
            p += SLOT;
        }
    }
    config_tables(mem);
    wu32(mem, c + 4440, ru32(mem, 8));
    {
        int v37 = c + 952;
        int nslots = (int)ru32(mem, 4);
        for (k = 0; k < nslots; k++) {
            zero_f(mem, (int)ru32(mem, v37 - 8), 128);
            zero_f(mem, (int)ru32(mem, v37), 748);
            zero_f(mem, (int)ru32(mem, v37 - 4), 336);
            zero_f(mem, (int)ru32(mem, v37 + 4), 308);
            zero_f(mem, v37 + 40, 60);
            zero_f(mem, v37 + 280, 36);
            for (i = 0; i < 40; i++)
                wu32(mem, v37 - 216 + 4 * i, 0xC2F00000u);
            v37 += SLOT;
        }
    }
}

EXPORT int dsee_mem_size(void) { return MEM_CTX + (SNAP_END - TBL_RVA0) + 0x400; }

static void load_tables(uint8_t *mem)
{
    static const struct {
        int rva;
        const float *src;
        int bytes;
    } blocks[] = {
        {0x1ADD8, dsee_up_44100_2x, sizeof dsee_up_44100_2x},
        {0x1AE60, dsee_up_48000_2x, sizeof dsee_up_48000_2x},
        {0x1AEE8, dsee_up_44100_4x, sizeof dsee_up_44100_4x},
        {0x1AFB0, dsee_up_48000_4x, sizeof dsee_up_48000_4x},
        {0x1B074, dsee_biq, sizeof dsee_biq},
        {0x1B088, dsee_common, sizeof dsee_common},
        {0x1B138, dsee_an_44100_2x, sizeof dsee_an_44100_2x},
        {0x1B3B0, dsee_an_48000_2x, sizeof dsee_an_48000_2x},
        {0x1B628, dsee_an_44100_4x, sizeof dsee_an_44100_4x},
        {0x1B9E0, dsee_an_48000_4x, sizeof dsee_an_48000_4x},
        {0x1BD98, dsee_sh, sizeof dsee_sh},
    };
    int i;
    for (i = 0; i < (int)(sizeof blocks / sizeof blocks[0]); i++)
        memcpy(mem + EXT_OFF + (blocks[i].rva - SNAP_RVA),
               blocks[i].src, (size_t)blocks[i].bytes);
    memcpy(mem + MEM_CTX, dsee_f76_coeff, sizeof dsee_f76_coeff);
}

static void work_rates(int in_fs, int out_fs, int *src, int *dst)
{
    if (in_fs <= 48000) {
        if (in_fs % 4000 != 0) {
            *src = 44100;
            if (out_fs <= 44100)
                *dst = 44100;
            else if (out_fs <= 88200)
                *dst = 88200;
            else
                *dst = 176400;
        } else {
            *src = 48000;
            if (out_fs <= 48000)
                *dst = 48000;
            else if (out_fs <= 96000)
                *dst = 96000;
            else
                *dst = 192000;
        }
    } else {
        *src = in_fs;
        *dst = out_fs > 192000 ? 192000 : out_fs;
    }
}

EXPORT int dsee_open_source(uint8_t *mem, int mem_size, const DseeSource *s,
                            DseeCfg *out)
{
    int st[16];
    int f76_init, i, rc;
    uint8_t hdr[32];
    int mode, codec, src, dst;

    if (!mem || !s || mem_size < dsee_mem_size())
        return 1;
    if (s->input_fs == 0 || s->output_fs == 0)
        return 1;
    if ((s->input_bits != 8 && s->input_bits != 16 && s->input_bits != 24 &&
         s->input_bits != 32) ||
        (s->output_bits != 8 && s->output_bits != 16 && s->output_bits != 24 &&
         s->output_bits != 32))
        return 1;
    codec = s->codec == 101 ? 16 : s->codec;
    mode = s->mode;
    work_rates(s->input_fs, s->output_fs, &src, &dst);

    memset(mem, 0, (size_t)mem_size);
    load_tables(mem);

    rc = check_params(hdr, s->channels, mode, src, dst, codec,
                      s->input_fs, s->vbr, s->abr, s->sb, s->head, s->dcr,
                      s->gain, s->offset);
    if (rc)
        return 10 + rc;
    parse_desc(hdr, st);
    f76_init = pick_f76(st);
    rc = decide(st, f76_init, out);
    if (rc)
        return 20 + rc;

    alloc_buffers(mem);
    f76_tables(mem, st[15]);
    wu32(mem, 0, 2);
    for (i = 0; i < 16; i++)
        wu32(mem, 4 + 4 * i, (uint32_t)st[i]);
    wu32(mem, 76, (uint32_t)out->f76);
    wu32(mem, 80, (uint32_t)out->f80);
    wu32(mem, 18964, (uint32_t)out->src);
    wu32(mem, 18968, (uint32_t)out->dst);
    wu32(mem, 18972, (uint32_t)out->rate);
    wu32(mem, 18976, (uint32_t)out->upmode);
    wu32(mem, 18988, (uint32_t)out->n12);
    wu32(mem, 18992, (uint32_t)out->f18992);
    wu32(mem, 18996, (uint32_t)out->step_src);
    wu32(mem, 19000, (uint32_t)out->step_dst);
    wu32(mem, 19076, (uint32_t)out->nblk);
    wu32(mem, 19080, (uint32_t)out->off_up);
    wu32(mem, 19084, (uint32_t)out->off_low);
    wf32(mem, 19096, out->gain_a);
    wu32(mem, 19100, (uint32_t)st[12]);
    wf32(mem, 19104, out->gain_b);
    wf32(mem, 19108, out->gain_c);
    wf32(mem, 19116, out->gain_e);
    wu32(mem, 19384, 3);

    wu32(mem, 68, (uint32_t)(out->upmode == 4 ? 236 : (out->upmode == 2 ? 134 : 0)));
    wu32(mem, 72, (uint32_t)out->step_src);

    if (out->f80 || out->f76)
        stream_init(mem, (const uint8_t *)dsee_seed, dsee_scale, dsee_bf);
    return 0;
}
