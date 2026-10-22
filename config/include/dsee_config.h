#ifndef DSEE_CONFIG_H
#define DSEE_CONFIG_H

#include "dsee_kern.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int rate;
    int channels;
    int bits;
    int bit_rate;
    char codec_name[64];
    int path_has_cbr;
} DseeMediaFacts;

DSEE_API int dsee_fill_source(const DseeMediaFacts *facts, int output_fs,
                              int output_bits, int enable, DseeSource *out);

#ifdef __cplusplus
}
#endif
#endif
