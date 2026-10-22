#pragma once

int config_map_codec(const char *codec_name);
void config_abr_vbr(const char *codec_name, int bit_rate_bps, int path_has_cbr,
                    int *abr_out, int *vbr_out);
