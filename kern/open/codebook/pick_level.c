#include "codebook.h"

static const int RATE_TAB[] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100,
    48000, 64000, 88200, 96000, 128000, 144000, 176400, 192000,
};

static int rate_index(int hz)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (RATE_TAB[i] == hz)
            return i;
    }
    return -1;
}

__declspec(dllexport) int dsee_pick_level(int channels, int vbr, int content_hz, int src_hz,
                           int codec, unsigned energy, int bit_override,
                           int *level)
{
    int content_idx = rate_index(content_hz);
    int src_idx = rate_index(src_hz);
    const CbEnt *root;
    const CbEnt *ent;
    int i, n, lv, matched;

    *level = 16;
    root = dsx_select_codebook_root(channels, vbr != 0, content_idx);
    if (!root || codec < 0 || codec > 17)
        return 0;
    ent = &root[codec];
    if (!ent->p || ent->n <= 0)
        return 0;

    matched = 0;
    lv = 16;
    for (i = 0, n = ent->n; i < n; i++) {
        if (energy >= (unsigned)(uint16_t)ent->p[i].th) {
            lv = ent->p[i].id;
            matched = 1;
            break;
        }
    }
    if (bit_override != 0)
        lv = bit_override;
    dsx_clamp_level(&lv, (unsigned)codec, content_idx);
    if (!matched || lv == 16) {
        *level = 16;
        return 0;
    }
    if (bit_override == 0 && (src_idx == 8 || src_idx == 11 || src_idx == 15)) {
        lv -= 1;
        if (lv < 8)
            lv = 8;
    }
    *level = lv;
    return 1;
}
