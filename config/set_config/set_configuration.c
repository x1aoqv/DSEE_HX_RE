#include "dsee_config.h"
#include "config_probe.h"

#include <string.h>

#define EXPORT __declspec(dllexport)

void config_default_overrides(int *sb, int *head, int *dcr, int *gain, int *offset);

EXPORT int dsee_fill_source(const DseeMediaFacts *facts, int output_fs,
                            int output_bits, int enable, DseeSource *out)
{
    int codec, abr, vbr;
    int sb, head, dcr, gain, offset;

    if (!facts || !out)
        return 1;
    if (facts->channels < 1 || facts->channels > 2)
        return 2;
    if (output_bits != 8 && output_bits != 16 && output_bits != 24 &&
        output_bits != 32)
        return 3;

    codec = config_map_codec(facts->codec_name);
    if (codec < 0)
        return 4; /* OpenMG id not in COmgDseeHxHelper allow-list / remap */
    config_abr_vbr(facts->codec_name, facts->bit_rate, facts->path_has_cbr, &abr,
                   &vbr);
    config_default_overrides(&sb, &head, &dcr, &gain, &offset);

    memset(out, 0, sizeof(*out));
    out->mode = dsee_mode(enable, output_fs, codec);
    out->channels = facts->channels;
    out->codec = codec;
    out->input_fs = facts->rate;
    out->output_fs = output_fs;
    out->input_bits = facts->bits;
    out->output_bits = output_bits;
    out->vbr = vbr;
    out->abr = abr;
    out->sb = sb;
    out->head = head;
    out->dcr = dcr;
    out->gain = gain;
    out->offset = offset;
    return 0;
}
