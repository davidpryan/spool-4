/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* SPOOL-4: four loops recorded by playing. Nothing is entered step by step: you play, and it keeps it.
 *   The first take on an empty loop sets its length: REC to start, REC again to close it at the nearest bar.
 *   Each pass recorded over it is a LAYER; UNDO takes the newest layer away (REDO brings it back).
 *   Quantise is not applied to the notes: it is how far each note is pulled to the grid as it plays (0..100 %),
 *   so it can be turned back at any time.
 *   CAPTURE: the keys are always heard and kept; ENV puts the last bars you played into the loop, recorded or not.
 * Notes are events (time in ticks of the loop, 96 a quarter; note; velocity; length; layer). */

#define NLOOP 4u
#define NEVT 448u
#define BAR 384u                                        /* ticks in a 4/4 bar */
#define MAXBARS 16u
enum { SP_HALF, SP_ONE, SP_TWO, NSPEED };
static const char *const SPEED_NM[NSPEED] = {"1/2", "x1", "x2"};
static const uint8_t GRID_T[] = {48, 32, 24, 16, 12};
static const char *const GRID_NM[] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"};
#define NGRID 5u

typedef struct {
    uint16_t t, len;                                    /* when in the loop, how long (ticks) */
    int8_t note;
    uint8_t vel, layer, rsv;
} evt_t;

typedef struct {
    uint16_t n;                                         /* events */
    uint8_t bars;                                       /* 0 empty (AUTO: the first take decides) */
    uint8_t layers, shown;                              /* layers recorded; layers heard (UNDO lowers it) */
    uint8_t q_amt, q_grid, speed;
    int8_t transpose;
    uint8_t muted, len_pref, rsv;                       /* len_pref: 0 AUTO, else the bars a new take gets */
    evt_t e[NEVT];
} loop_t;

#define PROJ_MAGIC 0x344C5053u                          /* "SPL4" */
#define PROJ_VER 1u
typedef struct {
    uint16_t bpm10;
    uint8_t clk_mode, clk_send, metro, count_in, echo_time, echo_fb;
    loop_t loop[NLOOP];
    patch_t ins[NLOOP];
    patch_t click;
    uint8_t rsv[16];
} proj_t;
static proj_t P PLAT_POOL;

enum { RS_OFF, RS_COUNT, RS_FIRST, RS_OVERDUB };
static struct {
    volatile uint8_t sel;                               /* the loop the keys record into */
    volatile uint8_t rec;                               /* RS_* */
    volatile uint8_t erase;                             /* FX held: notes passing the playhead go */
    uint32_t anchor[NLOOP];                             /* the transport tick a loop's 0 sits on */
    uint32_t rec_from;                                  /* RS_FIRST: the tick the take started */
    volatile int8_t held_note[128];                     /* a key held while recording: its event index + 1 per note */
    uint16_t held_ev[128];
    int16_t pos[NLOOP];                                 /* where each loop is (the screen) */
    /* the capture: every note played on the keys, with the transport tick */
    struct {
        uint32_t t;
        uint16_t len;
        int8_t note;
        uint8_t vel;
    } cap[128];
    uint32_t cap_w;
    uint32_t free_tick;                                 /* a tick that runs when stopped too (the capture's clock) */
    uint8_t repeat_on, repeat_note, repeat_vel;         /* ARP held with a key: note repeat */
} L;

static uint32_t loop_len(const loop_t *lp) { return (uint32_t)lp->bars * BAR; }

static uint32_t q_time(const loop_t *lp, uint32_t t)    /* where an event plays: pulled to the grid by q_amt */
{
    uint32_t g = GRID_T[lp->q_grid % NGRID], len = loop_len(lp), qt = (t + g / 2u) / g * g;
    int32_t d = ((int32_t)qt - (int32_t)t) * lp->q_amt / 100;
    return len ? (uint32_t)((int32_t)t + d + (int32_t)len) % len : t;
}

static void loop_play_at(uint32_t l, uint32_t pos)
{
    loop_t *lp = &P.loop[l];
    uint32_t i, sp = lp->speed == SP_TWO ? 2u : 1u, dv = lp->speed == SP_HALF ? 2u : 1u;
    for (i = 0; i < lp->n; i++) {
        evt_t *e = &lp->e[i];
        if (e->layer > lp->shown || e->len == 0u)
            continue;
        if (q_time(lp, e->t) != pos)
            continue;
        if (L.erase && l == L.sel) {                    /* FX held: it goes (its layer stays) */
            e->len = 0;
            continue;
        }
        if (!lp->muted)
            play_note(l, &P.ins[l], e->note + lp->transpose, e->vel, ticks_to_samples((uint32_t)e->len * dv / sp), 0);
    }
}

static void loops_tick(void)
{
    uint32_t l;
    for (l = 0; l < NLOOP; l++) {
        loop_t *lp = &P.loop[l];
        uint32_t len = loop_len(lp), rel;
        if (!len || T.tick < L.anchor[l])
            continue;
        rel = T.tick - L.anchor[l];
        if (lp->speed == SP_TWO) {
            loop_play_at(l, (2u * rel) % len);
            loop_play_at(l, (2u * rel + 1u) % len);
            L.pos[l] = (int16_t)((2u * rel) % len);
        } else if (lp->speed == SP_HALF) {
            if (!(rel & 1u))
                loop_play_at(l, (rel / 2u) % len);
            L.pos[l] = (int16_t)((rel / 2u) % len);
        } else {
            loop_play_at(l, rel % len);
            L.pos[l] = (int16_t)(rel % len);
        }
    }
}

static uint32_t rec_pos(void)                           /* the selected loop's position now, for a recorded note */
{
    const loop_t *lp = &P.loop[L.sel];
    uint32_t rel = T.tick - L.anchor[L.sel];
    if (L.rec == RS_FIRST)                              /* (played just before the take's bar: on its 1) */
        return T.tick < L.rec_from ? 0u : T.tick - L.rec_from;
    if (lp->speed == SP_TWO)
        return (2u * rel) % loop_len(lp);
    if (lp->speed == SP_HALF)
        return (rel / 2u) % loop_len(lp);
    return rel % loop_len(lp);
}

static void discard_hidden(loop_t *lp)                  /* a new layer: the undone ones are gone for good */
{
    uint32_t i, k = 0;
    for (i = 0; i < lp->n; i++)
        if (lp->e[i].layer <= lp->shown && lp->e[i].len)
            lp->e[k++] = lp->e[i];
    lp->n = (uint16_t)k;
    lp->layers = lp->shown;
}

/* recording: a key down / up (the render; from live_rec below) */
static void rec_note(int32_t note, uint32_t vel)
{
    loop_t *lp = &P.loop[L.sel];
    if (L.rec != RS_FIRST && L.rec != RS_OVERDUB)
        return;
    if (vel) {
        evt_t *e;
        if (lp->n >= NEVT)
            return;
        e = &lp->e[lp->n];
        e->t = (uint16_t)rec_pos();
        e->len = 1;
        e->note = (int8_t)(note - lp->transpose);
        e->vel = (uint8_t)vel;
        e->layer = lp->layers;
        L.held_note[note & 127] = 1;
        L.held_ev[note & 127] = lp->n;
        lp->n++;
    } else if (L.held_note[note & 127]) {
        evt_t *e = &lp->e[L.held_ev[note & 127]];
        uint32_t now = rec_pos(), len = L.rec == RS_FIRST ? MAXBARS * BAR : loop_len(lp);
        L.held_note[note & 127] = 0;
        e->len = (uint16_t)clamp((int32_t)((now + len - e->t) % len), 6, 0xFFFF);
    }
}

static void capture_note(int32_t note, uint32_t vel)    /* everything played is kept, recorded or not */
{
    uint32_t i;
    L.free_tick = (uint32_t)((float)T.now / samples_per_tick());   /* (a clock that runs when stopped too) */
    if (vel) {
        i = L.cap_w++ % 128u;
        L.cap[i].t = L.free_tick;
        L.cap[i].len = 0;
        L.cap[i].note = (int8_t)note;
        L.cap[i].vel = (uint8_t)vel;
        return;
    }
    for (i = 0; i < 128u; i++) {                        /* the newest open one of this note */
        uint32_t k = (L.cap_w - 1u - i) % 128u;
        if (L.cap[k].note == note && L.cap[k].len == 0u) {
            L.cap[k].len = (uint16_t)clamp((int32_t)(L.free_tick - L.cap[k].t), 6, 0xFFFF);
            break;
        }
    }
}

/* the keys, from the main loop through live_note's queue: heard, kept for capture, recorded */
#define KEY_OWNER 4u
static void key_note(int32_t note, uint32_t vel)
{
    live_note(KEY_OWNER, &P.ins[L.sel % NLOOP], note, vel, vel != 0u);
}

static void seq_midi_note(int32_t note, uint32_t vel, uint32_t chan)
{
    (void)chan;
    if (vel)
        play_note(KEY_OWNER, &P.ins[L.sel % NLOOP], note, vel, 0, 0);
    else
        note_end(KEY_OWNER, &P.ins[L.sel % NLOOP], note);
    rec_note(note, vel);
    capture_note(note, vel);
}

/* the live notes: the core calls play/end; recording and capture hook in here (the render) */
static void live_hook(const live_t *l)
{
    if (l->owner != KEY_OWNER)
        return;
    rec_note(l->note, l->on ? l->vel : 0u);
    capture_note(l->note, l->on ? l->vel : 0u);
}

/* the first take is closed: its length is the nearest whole bar (at least one) */
static void first_close(void)
{
    loop_t *lp = &P.loop[L.sel];
    uint32_t took = T.tick < L.rec_from ? 0u : T.tick - L.rec_from, bars = (took + BAR / 3u) / BAR, i, len;
    if (lp->len_pref)
        bars = lp->len_pref;
    if (bars < 1u)
        bars = 1u;
    if (bars > MAXBARS)
        bars = MAXBARS;
    lp->bars = (uint8_t)bars;
    len = loop_len(lp);
    for (i = 0; i < lp->n; i++) {                       /* notes past the end wrap round */
        lp->e[i].t = (uint16_t)(lp->e[i].t % len);
        if (lp->e[i].len > len)
            lp->e[i].len = (uint16_t)len;
    }
    L.anchor[L.sel] = L.rec_from;
    lp->shown = lp->layers;
}

static void click(uint32_t loud)
{
    if (P.metro || L.rec == RS_COUNT)
        play_note(5, &P.click, loud ? 76 : 37, loud ? 120 : 80, 0, 0);
}

static void seq_tick(void)
{
    if (T.tick % 96u == 0u && (P.metro || L.rec == RS_COUNT || L.rec == RS_FIRST))
        click(T.tick % BAR == 0u);
    if (L.rec == RS_COUNT && T.tick >= L.rec_from) {    /* the count-in is over */
        loop_t *lp = &P.loop[L.sel];
        if (lp->bars) {
            L.rec = RS_OVERDUB;
        } else {
            L.rec = RS_FIRST;
            L.anchor[L.sel] = L.rec_from;
            if (lp->len_pref)                           /* a fixed length: the take closes itself */
                lp->bars = lp->len_pref;
        }
    }
    if (L.rec == RS_FIRST && P.loop[L.sel].bars && T.tick - L.rec_from >= loop_len(&P.loop[L.sel])) {
        first_close();                                  /* (a fixed-length first take, full) */
        L.rec = RS_OVERDUB;
        P.loop[L.sel].layers++;
        P.loop[L.sel].shown = P.loop[L.sel].layers;
    }
    loops_tick();
    if (L.repeat_on && T.tick % GRID_T[P.loop[L.sel].q_grid % NGRID] == 0u) {   /* ARP held: note repeat */
        play_note(KEY_OWNER, &P.ins[L.sel], L.repeat_note, L.repeat_vel, ticks_to_samples(8), 0);
        rec_note(L.repeat_note, L.repeat_vel);
        rec_note(L.repeat_note, 0);
    }
}

static void seq_started(void)
{
    uint32_t l;
    for (l = 0; l < NLOOP; l++)
        L.anchor[l] = 0;
}
static void seq_stopped(void)
{
    if (L.rec == RS_FIRST)
        first_close();
    L.rec = RS_OFF;
}

/* ---- the main loop's actions (each takes the lock: the render reads the loop) ---- */
static void rec_press(void)
{
    loop_t *lp = &P.loop[L.sel];
    uint32_t f = plat_lock();
    if (L.rec == RS_FIRST) {                            /* the first take closes */
        first_close();
        L.rec = RS_OVERDUB;
        lp->layers++;
        lp->shown = lp->layers;
    } else if (L.rec == RS_OVERDUB || L.rec == RS_COUNT) {
        uint32_t i, n = 0;
        L.rec = RS_OFF;
        for (i = 0; i < lp->n; i++)                     /* a pass that added nothing is no layer */
            n += lp->e[i].layer == lp->layers;
        if (!n && lp->layers) {
            lp->layers--;
            if (lp->shown > lp->layers)
                lp->shown = lp->layers;
        }
    } else {
        discard_hidden(lp);
        lp->layers++;
        lp->shown = lp->layers;
        if (!T.playing) {                               /* from a stop: a bar's count-in (or none) */
            L.rec = RS_COUNT;
            L.rec_from = P.count_in ? BAR : 0u;
            T.want = 1;
        } else if (!lp->bars) {                         /* playing: the take starts at once, from this bar's line */
            L.rec = RS_FIRST;
            L.rec_from = T.tick - T.tick % BAR;
            if (T.tick % BAR > BAR * 3u / 4u)
                L.rec_from += BAR;                      /* (near the next bar: from that one) */
            L.anchor[L.sel] = L.rec_from;
            if (lp->len_pref)
                lp->bars = lp->len_pref;
        } else {
            L.rec = RS_OVERDUB;
        }
    }
    plat_unlock(f);
}

static int undo(void)
{
    loop_t *lp = &P.loop[L.sel];
    if (!lp->shown)
        return 0;
    lp->shown--;
    return 1;
}
static int redo(void)
{
    loop_t *lp = &P.loop[L.sel];
    if (lp->shown >= lp->layers)
        return 0;
    lp->shown++;
    return 1;
}

static void loop_clear(void)                            /* everything hidden: REDO brings it back until a new take */
{
    loop_t *lp = &P.loop[L.sel];
    uint32_t f = plat_lock();
    lp->shown = 0;
    if (L.rec)
        L.rec = RS_OFF;
    plat_unlock(f);
}

static int loop_double(void)                            /* the loop twice as long, its notes again in the new half */
{
    loop_t *lp = &P.loop[L.sel];
    uint32_t i, n = lp->n, len = loop_len(lp), f;
    if (!lp->bars || lp->bars * 2u > MAXBARS || n * 2u > NEVT)
        return 0;
    f = plat_lock();
    for (i = 0; i < n; i++) {
        lp->e[n + i] = lp->e[i];
        lp->e[n + i].t = (uint16_t)(lp->e[i].t + len);
    }
    lp->n = (uint16_t)(2u * n);
    lp->bars = (uint8_t)(lp->bars * 2u);
    plat_unlock(f);
    return 1;
}
static int loop_halve(void)                             /* the first half kept */
{
    loop_t *lp = &P.loop[L.sel];
    uint32_t i, k = 0, len, f;
    if (lp->bars < 2u)
        return 0;
    f = plat_lock();
    lp->bars = (uint8_t)(lp->bars / 2u);
    len = loop_len(lp);
    for (i = 0; i < lp->n; i++)
        if (lp->e[i].t < len)
            lp->e[k++] = lp->e[i];
    lp->n = (uint16_t)k;
    plat_unlock(f);
    return 1;
}

/* CAPTURE: the last bars played on the keys into the loop as a new layer (empty: as many bars as the playing took,
 * 1, 2, 4 or 8) */
static int capture(void)
{
    loop_t *lp = &P.loop[L.sel];
    uint32_t now, i, bars, len, first, f, got = 0, n_phrase = 0;
    L.free_tick = (uint32_t)((float)T.now / samples_per_tick());
    now = first = L.free_tick;
    for (i = 0; i < 128u && i < L.cap_w; i++) {          /* back to the phrase's start: a silence of a bar ends it */
        uint32_t k = (L.cap_w - 1u - i) % 128u;
        if (now - L.cap[k].t > 8u * BAR || first - L.cap[k].t > (i ? BAR : 2u * BAR))
            break;
        first = L.cap[k].t;
        n_phrase++;
    }
    if (!n_phrase)
        return 0;
    if (lp->bars) {
        bars = lp->bars;
    } else {
        uint32_t span = now - first;
        bars = span <= BAR ? 1u : span <= 2u * BAR ? 2u : span <= 4u * BAR ? 4u : 8u;
    }
    len = bars * BAR;
    f = plat_lock();
    discard_hidden(lp);
    lp->layers++;
    if (!lp->bars) {
        lp->bars = (uint8_t)bars;
        L.anchor[L.sel] = T.playing ? T.tick - T.tick % BAR : 0u;
    }
    for (i = 0; i < 128u && i < L.cap_w; i++) {
        uint32_t k = (L.cap_w - 1u - i) % 128u, age = now - L.cap[k].t;
        evt_t *e;
        if (i >= n_phrase || age > len || lp->n >= NEVT)
            break;
        e = &lp->e[lp->n++];
        /* the notes keep their place against the loop's playhead now */
        e->t = (uint16_t)(((uint32_t)(T.playing ? rec_pos() : 0u) + len * 8u - age) % len);
        e->len = L.cap[k].len ? L.cap[k].len : 48u;
        e->note = (int8_t)(L.cap[k].note - lp->transpose);
        e->vel = L.cap[k].vel;
        e->layer = lp->layers;
        got++;
    }
    lp->shown = lp->layers;
    plat_unlock(f);
    return (int)got;
}

static void loop_defaults(loop_t *lp)
{
    lp->n = 0;
    lp->bars = 0;
    lp->layers = lp->shown = 0;
    lp->q_amt = 0;
    lp->q_grid = 2;
    lp->speed = SP_ONE;
    lp->transpose = 0;
    lp->muted = 0;
    lp->len_pref = 0;
}

static void project_new(void)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)&P;
    for (i = 0; i < sizeof P; i++)
        b[i] = 0;
    P.bpm10 = 1000;
    P.clk_mode = CK_INTERNAL;
    P.clk_send = 1;
    P.metro = 0;
    P.count_in = 1;
    P.echo_time = 100;
    P.echo_fb = 55;
    for (i = 0; i < NLOOP; i++) {
        loop_defaults(&P.loop[i]);
        patch_default(&P.ins[i], PT_SYNTH, i + 1u);
    }
    patch_default(&P.ins[0], PT_DRUM, 10);              /* loop 1: drums, the keys a kit from F3 */
    P.ins[1].octave = -1;                               /* loop 2: a bass */
    P.ins[1].cutoff = 50;
    P.ins[1].reso = 70;
    P.ins[1].decay = 50;
    P.ins[1].sustain = 40;
    P.ins[2].wave = WV_TRI;                             /* loop 3: keys */
    P.ins[2].decay = 70;
    P.ins[2].sustain = 50;
    P.ins[2].echo = 50;
    P.ins[3].wave = WV_SUPER;                           /* loop 4: a pad */
    P.ins[3].attack = 60;
    P.ins[3].release = 80;
    P.ins[3].sustain = 100;
    P.ins[3].echo = 60;
    P.ins[3].level = 70;
    patch_default(&P.click, PT_DRUM, 0);
    P.click.level = 70;
    P.click.decay = 20;
}

static void project_apply(void)
{
    T.bpm10 = P.bpm10;
    T.clk_mode = P.clk_mode;
    T.clk_send = P.clk_send;
    echo_time = 0.05f + (float)P.echo_time * (0.32f / 127.0f);
    echo_fb = (float)P.echo_fb * (0.85f / 127.0f);
}
