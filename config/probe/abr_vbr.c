#include "config_probe.h"

#include <stdlib.h>
#include <string.h>

static int is_lossy_cbr_family(const char *n)
{
    return strcmp(n, "mp3") == 0 || strcmp(n, "mp2") == 0 ||
           strcmp(n, "aac") == 0 || strcmp(n, "wmav1") == 0 ||
           strcmp(n, "wmav2") == 0;
}

void config_abr_vbr(const char *codec_name, int bit_rate_bps, int path_has_cbr,
                    int *abr_out, int *vbr_out)
{
    const char *n = codec_name ? codec_name : "";
    int abr = 0;
    int vbr;

    if (bit_rate_bps > 0) {
        abr = bit_rate_bps / 1000;
        if (abr > 1020)
            abr = 1020;
    }

    if (strncmp(n, "pcm", 3) == 0)
        vbr = 0;
    else if (bit_rate_bps && is_lossy_cbr_family(n) &&
             abs(bit_rate_bps - abr * 1000) < 2000)
        vbr = 0;
    else
        vbr = (bit_rate_bps && path_has_cbr) ? 0 : 1;

    if (abr_out)
        *abr_out = abr;
    if (vbr_out)
        *vbr_out = vbr;
}
