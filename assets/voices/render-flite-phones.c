/* Offline companion to build-english-bank.py. Link against Flite 2.2 and cmu_us_kal16. */
#include "flite.h"
#include <stdio.h>

extern cst_voice *register_cmu_us_kal16(const char *);

int main(int argc, char **argv) {
    if (argc != 3) return 1;
    flite_init();
    cst_voice *voice = register_cmu_us_kal16(".");
    feat_set_float(voice->features, "int_f0_target_mean", 150);
    feat_set_float(voice->features, "int_f0_target_stddev", 0);
    feat_set_float(voice->features, "duration_stretch", 1.45);
    cst_utterance *utterance = flite_synth_phones(argv[1], voice);
    if (!utterance) return 2;
    cst_wave_save_riff(utt_wave(utterance), argv[2]);
    for (cst_item *i = relation_head(utt_relation(utterance, "Segment")); i; i = item_next(i))
        printf("%s %f\n", item_feat_string(i, "name"), item_feat_float(i, "end"));
    delete_utterance(utterance);
    return 0;
}
