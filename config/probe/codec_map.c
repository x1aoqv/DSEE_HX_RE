#include "config_probe.h"

#include <string.h>

/*
 * Music Center path (OmgPluginCommon COmgDseeHxHelper in Opc*):
 *   1) media library OpenMG CODEC_NAME_MAP id
 *   2) sub_6111AD5C remaps to filter SetConfiguration codec
 *   3) open_source folds 101 -> 16 before check_params / pick_level
 * Mode uses the value from step 2 (16 or 101 -> mode 2).
 */

static int omg_from_ffmpeg(const char *n)
{
    if (!n || !*n)
        return -1;
    if (strncmp(n, "pcm", 3) == 0)
        return 1;
    if (strcmp(n, "aac") == 0)
        return 2; /* AACLC; HE-AAC needs profile -> 24 */
    if (strcmp(n, "mp2") == 0)
        return 7;
    if (strcmp(n, "mp3") == 0)
        return 8;
    if (strcmp(n, "wmav1") == 0 || strcmp(n, "wmav2") == 0)
        return 14;
    if (strcmp(n, "wmalossless") == 0)
        return 19;
    if (strcmp(n, "wmapro") == 0)
        return 28;
    if (strcmp(n, "atrac3") == 0)
        return 16;
    if (strcmp(n, "atrac3p") == 0)
        return 18;
    if (strcmp(n, "flac") == 0)
        return 29;
    if (strcmp(n, "alac") == 0)
        return 31;
    if (strcmp(n, "ape") == 0)
        return 37;
    /* libfdk / some builds */
    if (strcmp(n, "libfdk_aac") == 0)
        return 2;
    return -1;
}

/* Official allow-list + remap (OpcFlac.dll sub_6111ACF6 / sub_6111AD5C). */
static int omg_to_filter(int omg)
{
    switch (omg) {
    case 1: /* PCM */
        return 16;
    case 2: /* AACLC */
        return 3;
    case 7:
    case 8:
    case 9:
    case 10: /* MPEG Layer */
        return 2;
    case 14: /* WMA */
        return 4;
    case 16: /* ATRAC3 */
        return 0;
    case 18: /* ATRAC3plus */
        return 1;
    case 19: /* WMA Lossless */
    case 22: /* ATRAC Advanced Lossless */
    case 23:
    case 29: /* FLAC */
    case 31: /* ALAC */
    case 37: /* APE */
        return 101;
    case 24: /* HE-AAC */
        return 5;
    case 28: /* WMA Pro */
        return 4;
    default:
        return -1;
    }
}

int config_map_codec(const char *codec_name)
{
    int omg = omg_from_ffmpeg(codec_name);
    if (omg < 0)
        return -1;
    return omg_to_filter(omg);
}
