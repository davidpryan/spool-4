/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* SPOOL-4's controls and screen.
 *   keys     always play the loop's sound (and are kept for CAPTURE); recorded while REC is lit
 *   ALGO     the loop      REC  record: the first take, then overdubs (a layer each)      PLAY  start / stop
 *   SEQ      UNDO the newest layer      SEL + SEQ  REDO      EDIT  mute the loop      SEL + EDIT  clear (REDO undoes)
 *   ENV      CAPTURE the last bars played      FX held  ERASE what passes the playhead      ARP held + key  repeat
 *   LFO      double the loop      SEL + LFO  halve it      OCT  the keys' octave      PRESETS  transpose the loop
 *   K1 quantise amount   K2 grid   K3 length of a new take (AUTO, bars)   K4 speed (half, normal, double)
 *   SEL tapped: the loop's SOUND page      GLO: SETUP (tempo, metronome, count-in, clock, slots)      HOME: LOOPS */

#define ACCENT RGB(255, 120, 150)
enum { PG_LOOPS, PG_SOUND, PG_SETUP };
static const char *const PG_NM[] = {"LOOPS", "SOUND", "SETUP"};
static const uint16_t LOOP_C[NLOOP] = {RGB(255, 120, 90), RGB(110, 200, 255), RGB(250, 215, 90), RGB(190, 140, 255)};
static const uint8_t LEN_PREF[] = {0, 1, 2, 4, 8, 16};

#define SET_MAGIC 0x3153504Cu
typedef struct {
    uint32_t magic;
    uint8_t slot, ble_off, rsv[2];
} settings_t;
static settings_t set;

static struct {
    uint8_t page, srow, grow;
    int8_t oct;
    uint8_t dirty, save_wait;
    int16_t key_note[NKEYS];
} U;

static const char *const CARD_TEXT[NB] = {
    "FX held: ERASE. Notes of the loop shown vanish as the playhead passes them, for as long as it is held.",
    ("SEL tapped: the loop's SOUND. Held: SEL + SEQ redo, SEL + EDIT clear the loop (redo brings it back), "
     "SEL + LFO halve it."),
    ("ENV: CAPTURE. What you just played on the keys, recorded or not, goes into the loop as a new layer. "
     "An empty loop takes as many bars as you played (1, 2, 4 or 8)."),
    "LFO doubles the loop: twice as long, its notes again in the new half. SEL + LFO halves it.",
    "EDIT mutes or unmutes the loop. SEL + EDIT clears it (SEL + SEQ, redo, brings it back).",
    "GLO: SETUP. Tempo, metronome, count-in, clock, echo and the four project slots.",
    "HOME: the LOOPS page, all four at once.",
    "SAVE writes the project to its slot. While playing it saves at the stop.",
    "ARP held with a key: the note repeats at the loop's grid (K2), recorded while REC is lit.",
    "SEQ: UNDO. The newest layer of the loop goes quiet. SEL + SEQ: REDO.",
    "PLAY starts and stops all four loops.",
    ("REC on an empty loop: the first take; REC again closes it at the nearest bar (or K3 sets the bars). "
     "On a loop with notes: overdub, a new layer each time."),
    "OCT- lowers the keys an octave.",
    "OCT+ raises the keys an octave.",
#if PLAT_SMK37
    0, 0, 0, 0,
#endif
};

static void dirty(void) { U.dirty = 1; }
static loop_t *lp_sel(void) { return &P.loop[L.sel % NLOOP]; }
static void settings_save(void) { plat_store_save(OBJ_SETTINGS, &set, sizeof set); }
int spool_ble_off(void) { return set.ble_off; }

static void project_save_now(void)
{
    int r;
    P.bpm10 = T.bpm10;
    r = slot_save(set.slot, PROJ_MAGIC, PROJ_VER, &P, sizeof P);
    if (r == 0)
        U.dirty = 0;
    app_say(r == 0 ? "SAVED" : "SAVE", r == 0 ? "" : r == -1 ? "TOO BIG" : "FAILED");
}
static void transport_wait_stop(void)
{
    uint32_t t0 = plat_ms();
    T.want = 2;
    while (T.playing && plat_ms() - t0 < 200u)
        plat_yield();
}

/* ---- SOUND rows (the selected loop's patch) ---- */
#define CUR_INS (&P.ins[L.sel % NLOOP])
static void v_u8(char *o, uint32_t v) { i_text((int32_t)v, o); }
static void ic(uint8_t *f, int32_t d, int32_t lo, int32_t hi) { *f = (uint8_t)clamp((int32_t)*f + d, lo, hi); dirty(); }
static void sv_type(char *o) { s_cpy(o, PT_TYPE_NM[CUR_INS->type % NPT_TYPE]); }
static void sv_wave(char *o) { s_cpy(o, CUR_INS->type == PT_DRUM ? "(KEYS PICK)" : WAVE_NM[CUR_INS->wave % NWAVE]); }
static void sv_cut(char *o) { v_u8(o, CUR_INS->cutoff); }
static void sv_res(char *o) { v_u8(o, CUR_INS->reso); }
static void sv_env(char *o) { v_u8(o, CUR_INS->envmod); }
static void sv_att(char *o) { v_u8(o, CUR_INS->attack); }
static void sv_dec(char *o) { v_u8(o, CUR_INS->decay); }
static void sv_sus(char *o) { v_u8(o, CUR_INS->sustain); }
static void sv_rel(char *o) { v_u8(o, CUR_INS->release); }
static void sv_lvl(char *o) { v_u8(o, CUR_INS->level); }
static void sv_echo(char *o) { v_u8(o, CUR_INS->echo); }
static void sv_oct(char *o) { o[0] = 0; sgn_cat(o, CUR_INS->octave); }
static void sv_chan(char *o) { if (CUR_INS->chan) v_u8(o, CUR_INS->chan); else s_cpy(o, "OFF"); }
static void sc_type(int32_t d) { ic(&CUR_INS->type, d, 0, NPT_TYPE - 1); }
static void sc_wave(int32_t d) { ic(&CUR_INS->wave, d, 0, NWAVE - 1); }
static void sc_cut(int32_t d) { ic(&CUR_INS->cutoff, d, 0, 127); }
static void sc_res(int32_t d) { ic(&CUR_INS->reso, d, 0, 127); }
static void sc_env(int32_t d) { ic(&CUR_INS->envmod, d, 0, 127); }
static void sc_att(int32_t d) { ic(&CUR_INS->attack, d, 0, 127); }
static void sc_dec(int32_t d) { ic(&CUR_INS->decay, d, 0, 127); }
static void sc_sus(int32_t d) { ic(&CUR_INS->sustain, d, 0, 127); }
static void sc_rel(int32_t d) { ic(&CUR_INS->release, d, 0, 127); }
static void sc_lvl(int32_t d) { ic(&CUR_INS->level, d, 0, 127); }
static void sc_echo(int32_t d) { ic(&CUR_INS->echo, d, 0, 127); }
static void sc_oct(int32_t d) { CUR_INS->octave = (int8_t)clamp(CUR_INS->octave + d, -3, 3); dirty(); }
static void sc_chan(int32_t d) { ic(&CUR_INS->chan, d, 0, 16); }
static const row_def_t SOUND_ROWS[] = {
    {"TYPE", sv_type, sc_type, 0},    {"WAVE", sv_wave, sc_wave, 0},      {"CUTOFF", sv_cut, sc_cut, 0},
    {"RESONANCE", sv_res, sc_res, 0}, {"ENV > FILTER", sv_env, sc_env, 0}, {"ATTACK", sv_att, sc_att, 0},
    {"DECAY", sv_dec, sc_dec, 0},     {"SUSTAIN", sv_sus, sc_sus, 0},     {"RELEASE", sv_rel, sc_rel, 0},
    {"LEVEL", sv_lvl, sc_lvl, 0},     {"ECHO SEND", sv_echo, sc_echo, 0}, {"OCTAVE", sv_oct, sc_oct, 0},
    {"MIDI CHANNEL", sv_chan, sc_chan, 0},
};
#define NSOUND (sizeof SOUND_ROWS / sizeof SOUND_ROWS[0])

/* ---- SETUP rows ---- */
static void gv_tempo(char *o) { o[0] = 0; i_cat(o, T.bpm10 / 10); s_cat(o, "."); i_cat(o, T.bpm10 % 10); }
static void gv_metro(char *o) { s_cpy(o, P.metro ? "ON" : "OFF (REC ONLY)"); }
static void gv_count(char *o) { s_cpy(o, P.count_in ? "1 BAR" : "NONE"); }
static void gv_clk(char *o) { s_cpy(o, CK_NM[T.clk_mode % NCK]); }
static void gv_clko(char *o) { s_cpy(o, T.clk_send ? "SEND" : "OFF"); }
static void gv_et(char *o) { v_u8(o, P.echo_time); }
static void gv_ef(char *o) { v_u8(o, P.echo_fb); }
static void gv_slot(char *o) { o[0] = 0; i_cat(o, set.slot + 1); s_cat(o, slot_used(set.slot, PROJ_MAGIC) ? "  (SAVED)" : "  (EMPTY)"); }
static void gv_ble(char *o) { s_cpy(o, set.ble_off ? "OFF" : "ON"); }
static void gc_tempo(int32_t d) { T.bpm10 = (uint16_t)clamp((int32_t)T.bpm10 + d * 10, 300, 3000); P.bpm10 = T.bpm10; dirty(); }
static void gc_metro(int32_t d) { P.metro = d > 0; dirty(); }
static void gc_count(int32_t d) { P.count_in = d > 0; dirty(); }
static void gc_clk(int32_t d) { T.clk_mode = (uint8_t)clamp(T.clk_mode + d, 0, NCK - 1); P.clk_mode = T.clk_mode; dirty(); }
static void gc_clko(int32_t d) { T.clk_send = d > 0; P.clk_send = T.clk_send; dirty(); }
static void gc_et(int32_t d) { ic(&P.echo_time, d, 0, 127); project_apply(); }
static void gc_ef(int32_t d) { ic(&P.echo_fb, d, 0, 127); project_apply(); }
static void gc_slot(int32_t d) { set.slot = (uint8_t)clamp(set.slot + d, 0, NSLOTS - 1); settings_save(); }
static void gc_ble(int32_t d) { set.ble_off = d < 0; plat_ble_enable(!set.ble_off); settings_save(); }
static void ga_save(void)
{
    if (T.playing) {
        U.save_wait = 1;
        app_say("SAVES", "AT THE STOP");
    } else {
        project_save_now();
    }
}
static void ga_load(void)
{
    if (T.playing)
        transport_wait_stop();
    if (slot_load(set.slot, PROJ_MAGIC, &P, sizeof P)) {
        project_apply();
        U.dirty = 0;
        app_say("LOADED", "");
    } else {
        app_say("SLOT", "EMPTY");
    }
}
static void ga_new(void)
{
    if (T.playing)
        transport_wait_stop();
    project_new();
    project_apply();
    dirty();
    app_say("NEW", "PROJECT");
}
static const row_def_t SETUP_ROWS[] = {
    {"TEMPO", gv_tempo, gc_tempo, 0},   {"METRONOME", gv_metro, gc_metro, 0}, {"COUNT-IN", gv_count, gc_count, 0},
    {"CLOCK", gv_clk, gc_clk, 0},       {"CLOCK OUT", gv_clko, gc_clko, 0},   {"ECHO TIME", gv_et, gc_et, 0},
    {"ECHO FEEDBACK", gv_ef, gc_ef, 0}, {"SLOT", gv_slot, gc_slot, 0},        {"SAVE TO SLOT", 0, 0, ga_save},
    {"LOAD SLOT", 0, 0, ga_load},       {"NEW PROJECT", 0, 0, ga_new},        {"BLUETOOTH", gv_ble, gc_ble, 0},
};
#define NSETUP (sizeof SETUP_ROWS / sizeof SETUP_ROWS[0])

static void input(void)
{
    uint32_t k;
    loop_t *lp;
    kit_input();
    lp = lp_sel();
    if (DOWN(B_PLAY)) {
        if (T.playing) {
            T.want = 2;
        } else {
            T.want = 1;
        }
    }
    if (DOWN(B_REC)) {
        rec_press();
        dirty();
    }
    if (DOWN(B_HOME))
        U.page = PG_LOOPS;
    if (TAPPED(B_GLO))
        U.page = U.page == PG_SETUP ? PG_LOOPS : PG_SETUP;
    if (TAPPED(B_SEL))
        U.page = U.page == PG_SOUND ? PG_LOOPS : PG_SOUND;
    if (DOWN(B_SEQ)) {
        int ok;
        if (HELD(B_SEL)) {
            kit_use(B_SEL);
            ok = redo();
            app_say("REDO", ok ? "LAYER BACK" : "NOTHING");
        } else {
            ok = undo();
            app_say("UNDO", ok ? "LAYER GONE" : "NOTHING");
        }
        dirty();
    }
    if (DOWN(B_EDIT)) {
        if (HELD(B_SEL)) {
            kit_use(B_SEL);
            loop_clear();
            app_say("CLEARED", "SEL+SEQ: BACK");
        } else {
            lp->muted ^= 1u;
        }
        dirty();
    }
    if (DOWN(B_LFO)) {
        int ok;
        if (HELD(B_SEL)) {
            kit_use(B_SEL);
            ok = loop_halve();
            app_say(ok ? "HALVED" : "HALVE", ok ? "" : "1 BAR ALREADY");
        } else {
            ok = loop_double();
            app_say(ok ? "DOUBLED" : "DOUBLE", ok ? "" : "NO ROOM");
        }
        dirty();
    }
    if (DOWN(B_ENV)) {
        int n = capture();
        char t[16] = "";
        if (n > 0) {
            i_cat(t, n);
            s_cat(t, " NOTES");
            dirty();
        }
        app_say("CAPTURED", n > 0 ? t : "NOTHING PLAYED");
    }
    L.erase = (uint8_t)HELD(B_FX);
    if (!HELD(B_ARP))
        L.repeat_on = 0;
    if (TAPPED(B_SAVE))
        ga_save();
    if (DOWN(B_OCTDN) || DOWN(B_OCTUP)) {
        char t[8] = "";
        U.oct = (int8_t)clamp(U.oct + (DOWN(B_OCTUP) ? 1 : -1), -3, 3);
        sgn_cat(t, U.oct);
        app_say("OCTAVE", t);
    }
    if (K.enc[EN_ALGO]) {
        uint32_t l = (uint32_t)clamp((int32_t)L.sel + K.enc[EN_ALGO], 0, NLOOP - 1);
        if (L.rec && l != L.sel)
            L.rec = RS_OFF;                             /* (a take stays in its own loop) */
        L.sel = (uint8_t)l;
    }
    if (U.page == PG_SOUND) {
        kit_list_input(SOUND_ROWS, NSOUND, &U.srow);
    } else if (U.page == PG_SETUP) {
        kit_list_input(SETUP_ROWS, NSETUP, &U.grow);
    } else {
        if (K.enc[EN_K1]) { lp->q_amt = (uint8_t)clamp(lp->q_amt + K.enc[EN_K1] * 5, 0, 100); dirty(); }
        if (K.enc[EN_K2]) { lp->q_grid = (uint8_t)clamp(lp->q_grid + K.enc[EN_K2], 0, NGRID - 1); dirty(); }
        if (K.enc[EN_K3]) {
            uint32_t i = 0;
            while (i < sizeof LEN_PREF - 1u && LEN_PREF[i] != lp->len_pref)
                i++;
            lp->len_pref = LEN_PREF[clamp((int32_t)i + K.enc[EN_K3], 0, (int32_t)sizeof LEN_PREF - 1)];
            dirty();
        }
        if (K.enc[EN_K4]) { lp->speed = (uint8_t)clamp(lp->speed + K.enc[EN_K4], 0, NSPEED - 1); dirty(); }
        if (K.enc[EN_PRESET]) { lp->transpose = (int8_t)clamp(lp->transpose + K.enc[EN_PRESET], -24, 24); dirty(); }
        if (K.enc[EN_SELECT]) gc_tempo(K.enc[EN_SELECT]);
    }
    for (k = 0; k < NKEYS; k++) {
        int32_t n = 53 + (int32_t)k + 12 * U.oct;
        if (K.kdown >> k & 1u) {
            if (HELD(B_ARP)) {                          /* note repeat */
                kit_use(B_ARP);
                L.repeat_note = (uint8_t)n;
                L.repeat_vel = K.vel[k];
                L.repeat_on = 1;
            } else {
                key_note(n, K.vel[k]);
                U.key_note[k] = (int16_t)n;
            }
        }
        if ((K.kup >> k & 1u)) {
            if (U.key_note[k] >= 0)
                key_note(U.key_note[k], 0);
            U.key_note[k] = -1;
            if (L.repeat_on && L.repeat_note == (uint8_t)n)
                L.repeat_on = 0;
        }
    }
    kit_cards(CARD_TEXT);
}

/* ---- the screen: four lanes, each a small piano roll of its loop ---- */
static void draw_lane(uint32_t l, int32_t y, int32_t h)
{
    const loop_t *lp = &P.loop[l];
    uint32_t len = loop_len(lp), i, lo = 127, hi = 0;
    int sel = l == L.sel;
    uint16_t c = lp->muted ? K_FAINT : LOOP_C[l];
    char t[32];
    int32_t x0 = 30, w = 206;
    cv_rect(0, y, 240, h - 2, sel ? K_PANEL : K_BG);
    cv_rect(0, y, 3, h - 2, c);
    t[0] = (char)('1' + l);
    t[1] = 0;
    cv_text(8, y + 1, &FONT_B, t, c);
    if (sel && L.rec) {
        cv_rect(10, y + 22, 9, 9, K_REC);
    }
    if (!len && !(sel && L.rec == RS_FIRST)) {
        cv_text(x0, y + 12, &FONT_T, sel ? "EMPTY: REC TO RECORD, ENV TO CAPTURE" : "EMPTY", K_FAINT);
        return;
    }
    if (!len)                                           /* the first take, still open */
        len = MAXBARS * BAR;
    for (i = 0; i < lp->n; i++)
        if (lp->e[i].layer <= lp->shown && lp->e[i].len) {
            if (lp->e[i].note < (int8_t)lo) lo = (uint32_t)lp->e[i].note;
            if (lp->e[i].note > (int8_t)hi) hi = (uint32_t)lp->e[i].note;
        }
    if (hi < lo)
        lo = hi = 60;
    if (hi - lo < 12u) {
        lo = lo > 6u ? lo - 6u : 0u;
        hi = lo + 12u;
    }
    for (i = 1; i < (lp->bars ? lp->bars : MAXBARS); i++)   /* bar lines */
        cv_rect(x0 + (int32_t)(i * BAR * (uint32_t)w / len), y + 2, 1, h - 6, K_LINE);
    for (i = 0; i < lp->n; i++) {
        const evt_t *e = &lp->e[i];
        int32_t ex, ew, ey;
        if (e->layer > lp->shown || !e->len)
            continue;
        ex = x0 + (int32_t)(q_time(lp, e->t) * (uint32_t)w / len);
        ew = (int32_t)((uint32_t)e->len * (uint32_t)w / len);
        if (ew < 2) ew = 2;
        if (ex + ew > x0 + w) ew = x0 + w - ex;
        ey = y + h - 6 - (int32_t)((uint32_t)(e->note - (int8_t)lo) * (uint32_t)(h - 10) / (hi - lo));
        cv_rect(ex, ey, ew, 3, e->layer == lp->layers && sel && L.rec ? K_FG : c);
    }
    if (T.playing && L.pos[l] >= 0 && lp->bars) {
        int32_t px = x0 + (int32_t)((uint32_t)L.pos[l] * (uint32_t)w / len);
        cv_rect(px, y + 1, 1, h - 4, L.erase && sel ? K_REC : K_FG);
    }
    /* its numbers */
    t[0] = 0;
    i_cat(t, lp->bars);
    s_cat(t, lp->bars == 1u ? " BAR" : " BARS");
    {
        char u[40] = "";
        s_cat(u, t);
        s_cat(u, "  L");
        i_cat(u, lp->shown);
        if (lp->shown < lp->layers) {
            s_cat(u, "/");
            i_cat(u, lp->layers);
        }
        if (lp->q_amt) {
            s_cat(u, "  Q");
            i_cat(u, lp->q_amt);
        }
        if (lp->speed != SP_ONE) {
            s_cat(u, "  ");
            s_cat(u, SPEED_NM[lp->speed]);
        }
        if (lp->transpose) {
            s_cat(u, "  ");
            sgn_cat(u, lp->transpose);
        }
        if (lp->muted)
            s_cat(u, "  MUTE");
        text_r(236, y + 1, &FONT_T, u, sel ? K_FG : K_DIM);
    }
}

static void draw(void)
{
    const char *rec_nm = L.rec == RS_COUNT ? "COUNT-IN" : L.rec == RS_FIRST ? "FIRST TAKE" : L.rec == RS_OVERDUB ? "OVERDUB" : 0;
    kit_header("SPOOL-4", rec_nm ? rec_nm : PG_NM[U.page], ACCENT, T.playing, L.rec != RS_OFF, T.bpm10);
    cv_begin(240, 200, K_BG);
    if (K.card < NB && CARD_TEXT[K.card]) {
        kit_card(K.card, CARD_TEXT[K.card], 200, ACCENT);
    } else if (U.page == PG_SOUND) {
        char t[16] = "LOOP ";
        i_cat(t, L.sel + 1);
        s_cat(t, " SOUND");
        cv_text(6, 0, &FONT_T, t, LOOP_C[L.sel % NLOOP]);
        kit_list_draw(SOUND_ROWS, NSOUND, U.srow, 14, 184, ACCENT);
    } else if (U.page == PG_SETUP) {
        cv_text(6, 0, &FONT_T, "SETUP", ACCENT);
        kit_list_draw(SETUP_ROWS, NSETUP, U.grow, 14, 184, ACCENT);
    } else {
        uint32_t l;
        for (l = 0; l < NLOOP; l++)
            draw_lane(l, 2 + (int32_t)l * 46, 46);
        if (L.rec == RS_COUNT && T.playing) {           /* the count-in's beats, large */
            char t[4];
            t[0] = (char)('1' + (T.tick / 96u) % 4u);
            t[1] = 0;
            cv_rect(90, 70, 60, 50, K_PANEL);
            text_c(120, 80, &FONT_L, t, K_REC);
        }
        cv_text(6, 186, &FONT_T, L.erase ? "ERASING AS IT PASSES" : "KEYS ALWAYS KEPT: ENV CAPTURES THEM", L.erase ? K_REC : K_FAINT);
    }
    cv_blit(0, 22);
    if (U.page == PG_LOOPS) {
        const loop_t *lp = lp_sel();
        char a[16] = "Q ", b[12] = "GRID ", c[16] = "TAKE ", d[12] = "SPEED ";
        i_cat(a, lp->q_amt);
        s_cat(a, "%");
        s_cat(b, GRID_NM[lp->q_grid % NGRID]);
        if (lp->len_pref) {
            i_cat(c, lp->len_pref);
            s_cat(c, " BAR");
        } else {
            s_cat(c, "AUTO");
        }
        s_cat(d, SPEED_NM[lp->speed % NSPEED]);
        kit_knobs(a, b, c, d, LOOP_C[L.sel % NLOOP]);
    } else {
        kit_knobs("VALUE", "", "", "", ACCENT);
    }
}

static void lights(void)
{
    uint32_t btn = 0;
    if (T.playing)
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    if (L.rec)
        btn |= 1u << B_REC;
    if (U.dirty)
        btn |= 1u << B_SAVE;
    if (lp_sel()->muted)
        btn |= 1u << B_EDIT;
    if (L.erase)
        btn |= 1u << B_FX;
    if (U.page == PG_SETUP)
        btn |= 1u << B_GLO;
    if (U.page == PG_LOOPS)
        btn |= 1u << B_HOME;
    plat_play_red(L.rec != RS_OFF);
    plat_lights(btn, K.keys, 0, 0, 1);
}
