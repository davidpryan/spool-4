/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* SPOOL-4: the unity build, after fm1-base's platform.
 *   core/   shared with the other five firmwares      seq.c  the loops and recording      ui.c  controls and screen */
#include "fastmath.h"
#define APP_LIVE_HOOK                            /* the keys' notes reach the recorder (core/clock.c) */
#include "core/util.c"
#include "core/synth.c"
#include "core/clock.c"
#include "core/slots.c"
#include "core/kit.c"
#include "seq.c"
#include "ui.c"

void app_init(void)
{
    uint32_t k;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC || set.slot >= NSLOTS) {
        set.magic = SET_MAGIC;
        set.slot = 0;
        set.ble_off = 0;
    }
    if (!slot_load(set.slot, PROJ_MAGIC, &P, sizeof P))
        project_new();
    project_apply();
    for (k = 0; k < NKEYS; k++)
        U.key_note[k] = -1;
    for (k = 0; k < NLOOP; k++)
        L.pos[k] = -1;
    U.page = PG_LOOPS;
    L.sel = 0;
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
    if ((T.playing || now - last >= 500u) && now - last >= 40u)
        K.redraw = 1;
    for (i = 0; i < 16u; i++)
        if (heard[i])
            heard[i]--;
    if (kit_hint_due(HINTS))
        K.redraw = 1;
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
    const loop_t *lp = &P.loop[L.sel % NLOOP];
    if (!strcmp(what, "playing")) *got = T.playing;
    else if (!strcmp(what, "page")) *got = U.page;
    else if (!strcmp(what, "sel")) *got = L.sel;
    else if (!strcmp(what, "rec")) *got = L.rec;
    else if (!strcmp(what, "bars")) *got = lp->bars;
    else if (!strcmp(what, "events")) *got = lp->n;
    else if (!strcmp(what, "layers")) *got = lp->layers;
    else if (!strcmp(what, "shown")) *got = lp->shown;
    else if (!strcmp(what, "heard")) {           /* events heard now (shown layers, not erased) */
        uint32_t i, n = 0;
        for (i = 0; i < lp->n; i++)
            n += lp->e[i].layer <= lp->shown && lp->e[i].len;
        *got = (int)n;
    }
    else if (!strcmp(what, "muted")) *got = lp->muted;
    else if (!strcmp(what, "dirty")) *got = U.dirty;
    else if (!strcmp(what, "card")) *got = K.card == NB ? -1 : K.card;
    else if (!strcmp(what, "sent")) *got = (int)midi_sent;
    else if (!strcmp(what, "note0")) *got = lp->n ? lp->e[0].note : -1;
    else return 0;
    return 1;
}
#endif
