#include "config_probe.h"

#include <string.h>

int config_map_codec(const char *codec_name)
{
    const char *n = codec_name ? codec_name : "";
    if (strncmp(n, "pcm", 3) == 0)
        return 1;
    if (strncmp(n, "dsd", 3) == 0)
        return 17;
    if (strcmp(n, "aac") == 0)
        return 2;
    if (strcmp(n, "mp3") == 0)
        return 8;
    if (strcmp(n, "mp2") == 0)
        return 7;
    if (strcmp(n, "wmav1") == 0 || strcmp(n, "wmav2") == 0)
        return 14;
    if (strcmp(n, "atrac3") == 0 || strcmp(n, "atrac3p") == 0)
        return 16;
    if (strcmp(n, "flac") == 0 || strcmp(n, "alac") == 0)
        return 16;

    return 16;
}
