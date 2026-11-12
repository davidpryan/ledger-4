/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* LEDGER-4: the unity build, after fm1-base's platform (gfx.c, announce.c, lib/touch.c are in scope).
 *   core/   shared with the other five firmwares: text, the synth, the transport, the slots, the controls
 *   seq.c   the song and its player      ui.c  the pages and controls      draw.c  the screen and lights */
#include "fastmath.h"
#include "core/util.c"
#include "core/synth.c"
#include "core/clock.c"
#include "core/slots.c"
#include "core/kit.c"
#include "seq.c"
#include "ui.c"
#include "draw.c"

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC || set.slot >= NSLOTS) {
        set.magic = SET_MAGIC;
        set.slot = 0;
        set.ble_off = 0;
    }
    if (!slot_load(set.slot, PROJ_MAGIC, &P, sizeof P))
        project_new();
    project_apply();
    U.page = PG_PHRASE;
    U.edit = 1;
    U.step = 1;
    U.phrase = 0;
    U.aud_note = -1;
    K.card = NB;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    clock_render(lr, n, (float)master_q12 * (1.0f / 4096.0f) * 7000000.0f);
}

void app_panic(void)
{
    T.playing = 0;
    voices_silence();
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, K_BG);
    K.redraw = 1;
}

void app_input_only(void) { input(); }

void app_frame(void)
{
    static uint32_t last;
    uint32_t now = plat_ms(), i;
    input();
    if (ann_on() && ann_where()) {
        char t[48];
        s_cpy(t, APP_NAME ". ");
        s_cat(t, PG_NM[U.page]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    if (U.save_wait && !T.playing) {
        U.save_wait = 0;
        project_save_now();
    }
    if (K.msg_until && now >= K.msg_until) {
        K.msg_until = 0;
        K.redraw = 1;
    }
    if (T.playing && now - last >= 40u)
        K.redraw = 1;
    for (i = 0; i < 16u; i++)
        if (heard[i])
            heard[i]--;
    lights();
    if (K.redraw) {
        K.redraw = 0;
        last = now;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing")) *got = T.playing;
    else if (!strcmp(what, "page")) *got = U.page;
    else if (!strcmp(what, "prow")) *got = U.prow;
    else if (!strcmp(what, "pcol")) *got = U.pcol;
    else if (!strcmp(what, "phrase")) *got = U.phrase;
    else if (!strcmp(what, "chain")) *got = U.chain;
    else if (!strcmp(what, "dirty")) *got = U.dirty;
    else if (!strcmp(what, "card")) *got = K.card == NB ? -1 : K.card;
    else if (!strcmp(what, "voices")) *got = voices_busy();
    else if (!strcmp(what, "sent")) *got = (int)midi_sent;
    else if (!strcmp(what, "bpm10")) *got = T.bpm10;
    else if (!strcmp(what, "t0prow")) *got = E.t[0].prow;
    else if (!strcmp(what, "t1srow")) *got = E.t[1].srow;
    else if (!strncmp(what, "note", 4)) *got = P.phr[U.phrase].r[atoi(what + 4) % NROW].note;
    else if (!strncmp(what, "fx", 2)) *got = P.phr[U.phrase].r[atoi(what + 2) % NROW].fx;
    else if (!strncmp(what, "val", 3)) *got = P.phr[U.phrase].r[atoi(what + 3) % NROW].val;
    else if (!strncmp(what, "song", 4)) *got = P.song[atoi(what + 4) % NSONG][0];
    else return 0;
    return 1;
}
#endif
