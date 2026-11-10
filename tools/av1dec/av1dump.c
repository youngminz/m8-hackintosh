// av1dump FILE.obu [N]: parse an OBU stream (ffmpeg -f obu) and print the frame headers.
#include "av1hdr.h"
#include <stdio.h>
#include <stdlib.h>
static int n, limit = 1000000;
static void cb(void *ctx, Av1Parser *p, const Av1Frame *f, const uint8_t *d) {
    if (n++ < limit) {
        printf("frame %d: type %d show %d existing %d(%d) %dx%d order %d primary %d refresh %02x q %d tiles %dx%d (%d found, size bytes %d) lf %d/%d cdef bits %d damping %d lr %d%d%d txmode %d refsel %d skipmode %d warped %d interp %d",
               n - 1, f->frame_type, f->show_frame, f->show_existing_frame, f->frame_to_show_map_idx, f->frame_width, f->frame_height, f->order_hint,
               f->primary_ref_frame, f->refresh_frame_flags, f->base_q_idx, f->tile_cols, f->tile_rows, f->num_tiles_found, f->tile_size_bytes,
               f->loop_filter_level[0], f->loop_filter_level[1], f->cdef_bits, f->cdef_damping, f->lr_type[0], f->lr_type[1], f->lr_type[2], f->tx_mode,
               f->reference_select, f->skip_mode_present, f->allow_warped_motion, f->interpolation_filter);
        if (!f->frame_is_intra && !f->show_existing_frame) { printf(" refs"); for (int i = 0; i < 7; i++) printf(" %d", f->ref_frame_idx[i]); printf(" gm"); for (int i = 1; i < 8; i++) printf(" %d", f->gm_type[i]); }
        if (f->num_tiles_found) printf(" tile0 @%u+%u last @%u+%u", f->tile_offset[0], f->tile_size[0], f->tile_offset[f->num_tiles_found - 1], f->tile_size[f->num_tiles_found - 1]);
        printf("\n");
    }
    av1_update_refs(p, f);
}
int main(int argc, char **argv) {
    FILE *fp = fopen(argv[1], "rb"); if (!fp) return 1;
    if (argc > 2) limit = atoi(argv[2]);
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *d = malloc(sz); fread(d, 1, sz, fp);
    static Av1Parser p;
    int r = av1_parse_temporal_unit(&p, d, sz, cb, NULL);
    const Av1Seq *s = &p.seq;
    printf("result %d, %d frames; seq profile %d %dx%d depth %d sb128 %d order_hint_bits %d cdef %d lr %d superres %d filmgrain %d\n", r, n, s->profile,
           s->max_frame_width, s->max_frame_height, s->bit_depth, s->use_128x128_superblock, s->order_hint_bits, s->enable_cdef, s->enable_restoration, s->enable_superres, s->film_grain_params_present);
    return 0;
}
