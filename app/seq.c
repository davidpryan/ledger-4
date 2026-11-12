/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* LEDGER-4's song and its player. A tracker in the line of Little Sound DJ and the M8:
 *   SONG    64 rows x 4 tracks, each cell a chain (or empty)
 *   CHAIN   16 rows, each a phrase and a transpose
 *   PHRASE  16 rows, each NOTE, INSTRUMENT, VELOCITY, a COMMAND and its VALUE
 * A row lasts a groove's ticks (6 is a sixteenth at the tempo, as in LSDJ; two values alternate for swing). Each track
 * walks its own column of the song, so chains of different lengths drift against each other; an empty cell below a
 * track's block sends it back to the block's first row.
 * The commands (one per row, the value in hex) are what make a tracker more than a list of notes: arpeggio, chance,
 * delay, kill, retrigger, slide, tempo, hop, filter, MIDI CC, random and volume ramps. */

#define NTRK 4u
#define NPHR 128u
#define NCHN 128u
#define NSONG 64u
#define NROW 16u
#define NINS 16u
#define EMPTY 0xFFu
#define NOTE_OFF 0x80u                                  /* a row's note: 0 none, 1..127 a note, 0x80 note off */

enum { FX_NONE, FX_ARP, FX_CHANCE, FX_DELAY, FX_KILL, FX_RETRIG, FX_SLIDE, FX_TEMPO, FX_HOP, FX_FILTER, FX_MIDICC,
       FX_RANDOM, FX_GROOVE, FX_ECHO, NFX };
static const char FX_CH[NFX] = {'-', 'A', 'C', 'D', 'K', 'R', 'S', 'T', 'H', 'F', 'M', 'N', 'G', 'E'};
static const char *const FX_NM[NFX] = {"NO COMMAND", "ARPEGGIO", "CHANCE", "DELAY", "KILL", "RETRIGGER", "SLIDE",
                                       "TEMPO", "HOP", "FILTER", "MIDI CC", "RANDOM", "GROOVE", "ECHO"};

typedef struct {
    uint8_t note, ins, vel, fx, val;
} row_t;
typedef struct {
    row_t r[NROW];
} phrase_t;
typedef struct {
    uint8_t phr[NROW];                                  /* EMPTY none */
    int8_t tsp[NROW];
} chain_t;

#define PROJ_MAGIC 0x3452474Cu                          /* "LGR4" */
#define PROJ_VER 1u
typedef struct {
    uint16_t bpm10;
    uint8_t groove[2];                                  /* ticks per row, even rows then odd (6/6 straight) */
    uint8_t clk_mode, clk_send;
    uint8_t echo_time, echo_fb;                         /* 0..127 */
    uint8_t song[NSONG][NTRK];
    chain_t chn[NCHN];
    phrase_t phr[NPHR];
    patch_t ins[NINS];
    uint8_t trk_mute;                                   /* bit per track */
    uint8_t rsv[31];
} proj_t;
static proj_t P PLAT_POOL;

/* ---- the player ---- */
enum { PM_SONG, PM_CHAIN, PM_PHRASE };
typedef struct {
    uint8_t srow, crow, prow;                           /* where it is: song row, chain row, phrase row */
    uint8_t active;                                     /* this track plays */
    uint8_t chain, phrase;                              /* what it plays (for the screen) */
    uint8_t row_ticks;                                  /* groove ticks into the row */
    uint8_t row_len;                                    /* the row's ticks */
    uint8_t hop;                                        /* a hop waits: the next phrase starts at row hop-1 (0 none) */
    /* the row's command, run on its ticks */
    uint8_t fx, val, note, ins, vel, pending;           /* pending: the note waits for its delay */
    int8_t tsp;
    int16_t sounding;                                   /* the note sounding (-1 none) */
    patch_t pt;                                         /* the instrument as this note plays it (S, F change it) */
    uint32_t loops;
} play_t;
static struct {
    uint8_t mode;                                       /* PM_* */
    uint8_t solo_chain, solo_phrase, solo_track;        /* what CHAIN / PHRASE play loops */
    uint32_t seed;
    play_t t[NTRK];
} E;

static uint32_t sub4;                                   /* core ticks (96 a beat) into a groove tick (24 a beat) */

static void stop_track(uint32_t t)
{
    play_t *p = &E.t[t];
    if (p->sounding >= 0) {
        note_end(t, &p->pt, p->sounding);
        p->sounding = -1;
    }
}

static int song_next_row(uint32_t t, uint32_t r)       /* the row after r for track t (its block wraps) */
{
    uint32_t n = r + 1u;
    if (n < NSONG && P.song[n][t] != EMPTY)
        return (int)n;
    while (r > 0u && P.song[r - 1u][t] != EMPTY)
        r--;
    return (int)r;
}

static int chain_first_row(uint32_t c)
{
    uint32_t r;
    for (r = 0; r < NROW; r++)
        if (P.chn[c].phr[r] != EMPTY)
            return (int)r;
    return -1;
}

/* the track has finished a phrase: on to the chain's next row (and the song's, at the chain's end) */
static void advance_chain(uint32_t t)
{
    play_t *p = &E.t[t];
    uint32_t r;
    if (E.mode == PM_PHRASE)
        return;
    for (r = p->crow + 1u; r < NROW; r++)
        if (P.chn[p->chain].phr[r] != EMPTY) {
            p->crow = (uint8_t)r;
            return;
        }
    /* the chain is done */
    p->loops++;
    if (E.mode == PM_SONG) {
        p->srow = (uint8_t)song_next_row(t, p->srow);
        p->chain = P.song[p->srow][t];
    }
    p->crow = (uint8_t)(chain_first_row(p->chain) < 0 ? 0 : chain_first_row(p->chain));
}

static void load_row_source(uint32_t t)                /* the chain and phrase now under the track's position */
{
    play_t *p = &E.t[t];
    if (E.mode == PM_SONG) {
        p->chain = P.song[p->srow][t];
        if (p->chain == EMPTY) {
            p->active = 0;
            return;
        }
    }
    if (E.mode != PM_PHRASE) {
        if (chain_first_row(p->chain) < 0) {
            p->active = 0;
            return;
        }
        if (P.chn[p->chain].phr[p->crow] == EMPTY)
            p->crow = (uint8_t)chain_first_row(p->chain);
        p->phrase = P.chn[p->chain].phr[p->crow];
        p->tsp = P.chn[p->chain].tsp[p->crow];
    } else {
        p->tsp = 0;
    }
}


static void sound_note(uint32_t t, uint32_t vel)
{
    play_t *p = &E.t[t];
    int32_t n = (int32_t)p->note;
    if (p->note == NOTE_OFF) {
        stop_track(t);
        return;
    }
    if (!p->note)
        return;
    if ((P.trk_mute >> t) & 1u)
        return;
    if (p->pt.type != PT_DRUM)
        n += p->tsp;
    if (p->fx == FX_RANDOM && p->val)
        n += (int32_t)(xs32(&E.seed) % ((uint32_t)p->val + 1u));
    n = clamp(n, 0, 127);
    if (p->fx != FX_SLIDE)
        stop_track(t);
    else if (p->sounding >= 0 && p->sounding != n)
        note_end(t, &p->pt, p->sounding);
    play_note(t, &p->pt, n, vel, 0, p->fx == FX_SLIDE);
    p->sounding = (int16_t)n;
}

/* the row starts: read it, then its command decides what happens on which tick */
static void row_start(uint32_t t)
{
    play_t *p = &E.t[t];
    const row_t *r;
    load_row_source(t);
    if (!p->active)
        return;
    r = &P.phr[p->phrase].r[p->prow];
    p->row_ticks = 0;
    p->row_len = P.groove[p->prow & 1u];           /* even rows, odd rows: the swing */
    p->fx = r->fx;
    p->val = r->val;
    p->pending = 0;
    if (r->note) {
        p->note = r->note;
        if (r->ins != EMPTY)
            p->ins = r->ins < NINS ? r->ins : 0;
        p->vel = r->vel != EMPTY ? r->vel : 100;
        p->pt = P.ins[p->ins];
        if (p->fx == FX_SLIDE)
            p->pt.glide = (uint8_t)(p->val ? p->val / 2u : 20u);
        else
            p->pt.glide = 0;
        if (p->fx == FX_FILTER)
            p->pt.cutoff = (uint8_t)(p->val / 2u);
        if (p->fx == FX_ECHO)
            p->pt.echo = (uint8_t)(p->val / 2u);
        if (p->fx == FX_CHANCE && (xs32(&E.seed) % 100u) >= (uint32_t)(p->val > 99u ? 100u : p->val))
            p->note = 0;                                /* (C: the value is the percent, 00..64 hex = 0..100) */
        p->pending = 1;
    } else if (r->vel != EMPTY && p->sounding >= 0) {
        p->vel = r->vel;                                /* a velocity alone: the next retrigs */
    }
    switch (p->fx) {                                    /* the commands that act at the row's start */
    case FX_TEMPO:
        if (p->val >= 30u)
            T.bpm10 = (uint16_t)(p->val * 10u);
        break;
    case FX_MIDICC:
        if (P.ins[p->ins].chan && P.ins[p->ins].cc)
            midi_out(0xB0u | (P.ins[p->ins].chan - 1u), P.ins[p->ins].cc, p->val / 2u);
        break;
    case FX_GROOVE:
        P.groove[0] = (uint8_t)clamp(p->val >> 4, 1, 15);
        P.groove[1] = (uint8_t)clamp(p->val & 15u, 1, 15);
        break;
    case FX_HOP:
        p->hop = (uint8_t)((p->val & 15u) + 1u);
        break;
    default: break;
    }
}

/* a groove tick of the row (24 a beat) */
static void row_tick(uint32_t t)
{
    play_t *p = &E.t[t];
    uint32_t k = p->row_ticks, x = p->val >> 4, y = p->val & 15u;
    if (!p->active)
        return;
    if (p->pending) {
        uint32_t wait = p->fx == FX_DELAY ? p->val : 0u;
        if (k >= wait) {
            p->pending = 0;
            sound_note(t, p->vel);
        }
    } else if (p->sounding >= 0 && p->note && p->note != NOTE_OFF) {
        switch (p->fx) {
        case FX_ARP: {                                  /* note, +x, +y on every tick */
            static const uint8_t SEQ3[3] = {0, 1, 2};
            uint32_t ph = SEQ3[k % 3u];
            int32_t n = (int32_t)p->note + (p->pt.type == PT_DRUM ? 0 : p->tsp) + (ph == 1u ? (int32_t)x : ph == 2u ? (int32_t)y : 0);
            if (!((P.trk_mute >> t) & 1u)) {
                note_end(t, &p->pt, p->sounding);
                play_note(t, &p->pt, n, p->vel, 0, 0);
                p->sounding = (int16_t)n;
            }
            break;
        }
        case FX_RETRIG:                                 /* every x ticks (or y if x is 0), louder or softer by y */
            if ((x ? x : 1u) && k % (x ? x : 1u) == 0u) {
                int32_t v = (int32_t)p->vel + (x ? ((int32_t)y - 8) * 6 : 0);
                p->vel = (uint8_t)clamp(v, 8, 127);
                sound_note(t, p->vel);
            }
            break;
        case FX_KILL:
            if (k >= p->val)
                stop_track(t);
            break;
        default: break;
        }
    }
    p->row_ticks++;
    if (p->row_ticks >= p->row_len) {                   /* the row is done: the next */
        p->prow++;
        if (p->hop)                                     /* H: the phrase ends after this row */
            p->prow = NROW;
        if (p->prow >= NROW) {
            p->prow = p->hop ? (uint8_t)(p->hop - 1u) : 0u;
            p->hop = 0;
            advance_chain(t);
        }
        row_start(t);
    }
}

static void seq_started(void)
{
    uint32_t t;
    sub4 = 0;
    E.seed = 0x51u;
    for (t = 0; t < NTRK; t++) {
        play_t *p = &E.t[t];
        p->prow = 0;
        p->crow = 0;
        p->hop = 0;
        p->loops = 0;
        p->sounding = -1;
        p->ins = 0;
        p->note = 0;
        p->active = 1;
        if (E.mode == PM_SONG) {
            p->chain = P.song[p->srow][t];
            p->crow = (uint8_t)(p->chain != EMPTY && chain_first_row(p->chain) >= 0 ? chain_first_row(p->chain) : 0);
        } else {
            p->active = t == E.solo_track;
            p->chain = E.solo_chain;
            p->phrase = E.solo_phrase;
            if (E.mode == PM_CHAIN)
                p->crow = (uint8_t)(chain_first_row(p->chain) < 0 ? 0 : chain_first_row(p->chain));
        }
        if (p->active)
            row_start(t);
    }
}

static void seq_stopped(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        stop_track(t);
        E.t[t].active = 0;
    }
}

static void seq_tick(void)
{
    uint32_t t;
    if (++sub4 < 4u)                                    /* a groove tick is 4 of the core's (24 a beat) */
        return;
    sub4 = 0;
    for (t = 0; t < NTRK; t++)
        row_tick(t);
}

/* MIDI in: notes play the instrument under the cursor (the UI sets it) */
static volatile uint8_t midi_ins;
static void seq_midi_note(int32_t note, uint32_t vel, uint32_t chan)
{
    (void)chan;
    if (vel)
        play_note(7, &P.ins[midi_ins % NINS], note, vel, 0, 0);
    else
        note_end(7, &P.ins[midi_ins % NINS], note);
}

/* ---- a new project: a short song that plays at once ---- */
static void ins_defaults(void)
{
    uint32_t i;
    for (i = 0; i < NINS; i++)
        patch_default(&P.ins[i], PT_SYNTH, (i % 4u) + 1u);
    patch_default(&P.ins[0], PT_DRUM, 10);              /* 00 the drums (GM channel 10) */
    P.ins[1].wave = WV_SAW;                              /* 01 a bass */
    P.ins[1].octave = -2;
    P.ins[1].cutoff = 40;
    P.ins[1].reso = 80;
    P.ins[1].envmod = 70;
    P.ins[1].decay = 45;
    P.ins[1].sustain = 20;
    P.ins[1].chan = 2;
    P.ins[2].wave = WV_SQR;                              /* 02 a lead */
    P.ins[2].cutoff = 80;
    P.ins[2].echo = 50;
    P.ins[2].decay = 55;
    P.ins[2].sustain = 50;
    P.ins[2].chan = 3;
    P.ins[3].wave = WV_SUPER;                            /* 03 a pad */
    P.ins[3].attack = 70;
    P.ins[3].release = 85;
    P.ins[3].cutoff = 60;
    P.ins[3].sustain = 100;
    P.ins[3].echo = 40;
    P.ins[3].level = 70;
    P.ins[3].chan = 4;
    P.ins[4].wave = WV_TRI;                              /* 04 a pluck */
    P.ins[4].decay = 30;
    P.ins[4].sustain = 0;
    P.ins[4].echo = 70;
    P.ins[5].wave = WV_SINE;                             /* 05 a sub */
    P.ins[5].octave = -1;
    P.ins[5].decay = 70;
}

static void rows_clear(phrase_t *ph)
{
    uint32_t r;
    for (r = 0; r < NROW; r++) {
        ph->r[r].note = 0;
        ph->r[r].ins = EMPTY;
        ph->r[r].vel = EMPTY;
        ph->r[r].fx = FX_NONE;
        ph->r[r].val = 0;
    }
}
static void chain_clear(chain_t *c)
{
    uint32_t r;
    for (r = 0; r < NROW; r++) {
        c->phr[r] = EMPTY;
        c->tsp[r] = 0;
    }
}

static void put(uint32_t ph, uint32_t r, uint32_t note, uint32_t ins, uint32_t vel, uint32_t fx, uint32_t val)
{
    row_t *w = &P.phr[ph].r[r];
    w->note = (uint8_t)note;
    w->ins = (uint8_t)ins;
    w->vel = (uint8_t)vel;
    w->fx = (uint8_t)fx;
    w->val = (uint8_t)val;
}

static void project_new(void)
{
    uint32_t i, r;
    uint8_t *b = (uint8_t *)&P;
    for (i = 0; i < sizeof P; i++)
        b[i] = 0;
    P.bpm10 = 1200;
    P.groove[0] = P.groove[1] = 6;
    P.clk_mode = CK_INTERNAL;
    P.clk_send = 1;
    P.echo_time = 80;
    P.echo_fb = 60;
    for (i = 0; i < NSONG; i++)
        for (r = 0; r < NTRK; r++)
            P.song[i][r] = EMPTY;
    for (i = 0; i < NCHN; i++)
        chain_clear(&P.chn[i]);
    for (i = 0; i < NPHR; i++)
        rows_clear(&P.phr[i]);
    ins_defaults();
    /* phrase 00: a beat; 01: a fill; 02: a bass line; 03: a lead; 04: a chord pad */
    for (r = 0; r < NROW; r += 4)
        put(0, r, 36, 0, 120, FX_NONE, 0);
    put(0, 4, 38, 0, 110, FX_NONE, 0);
    put(0, 12, 38, 0, 110, FX_NONE, 0);
    for (r = 2; r < NROW; r += 4)
        put(0, r, 42, 0, 80, FX_NONE, 0);
    put(0, 14, 42, 0, 70, FX_RETRIG, 0x2A);
    for (r = 0; r < NROW; r++)
        P.phr[1].r[r] = P.phr[0].r[r];
    put(1, 12, 38, 0, 90, FX_RETRIG, 0x19);
    put(1, 13, 38, 0, 100, FX_NONE, 0);
    put(1, 15, 39, 0, 120, FX_CHANCE, 60);
    {
        static const uint8_t BASS[16] = {45, 0, 45, 57, 0, 45, 0, 43, 45, 0, 52, 0, 55, 0, 57, 0};
        for (r = 0; r < NROW; r++)
            if (BASS[r])
                put(2, r, BASS[r] + 12u, 1, r % 4u ? 90 : 120, r == 3u || r == 14u ? FX_SLIDE : FX_NONE, 0x18);
    }
    put(3, 0, 69, 2, 100, FX_NONE, 0);
    put(3, 3, 72, EMPTY, 90, FX_SLIDE, 0x20);
    put(3, 6, 76, EMPTY, 95, FX_NONE, 0);
    put(3, 8, 74, EMPTY, 100, FX_ARP, 0x37);
    put(3, 11, 72, EMPTY, 80, FX_DELAY, 3);
    put(3, 12, NOTE_OFF, EMPTY, EMPTY, FX_NONE, 0);
    put(3, 14, 67, EMPTY, 90, FX_RANDOM, 5);
    put(4, 0, 57, 3, 80, FX_ARP, 0x37);
    put(4, 8, 55, 3, 80, FX_ARP, 0x47);
    /* chains: 00 beat x3 + fill; 01 bass, then up a fourth; 02 lead and rest; 03 pad */
    P.chn[0].phr[0] = 0;
    P.chn[0].phr[1] = 0;
    P.chn[0].phr[2] = 0;
    P.chn[0].phr[3] = 1;
    P.chn[1].phr[0] = 2;
    P.chn[1].phr[1] = 2;
    P.chn[1].phr[2] = 2;
    P.chn[1].tsp[2] = 5;
    P.chn[1].phr[3] = 2;
    P.chn[1].tsp[3] = 3;
    P.chn[2].phr[0] = 3;
    P.chn[2].phr[1] = 3;
    P.chn[2].tsp[1] = -2;
    P.chn[3].phr[0] = 4;
    for (i = 0; i < 2u; i++) {
        P.song[i][0] = 0;
        P.song[i][1] = 1;
        P.song[i][3] = 3;
    }
    P.song[1][2] = 2;
}

static void project_apply(void)                        /* the project's settings into the transport */
{
    T.bpm10 = P.bpm10;
    T.clk_mode = P.clk_mode;
    T.clk_send = P.clk_send;
    echo_time = 0.05f + (float)P.echo_time * (0.32f / 127.0f);
    echo_fb = (float)P.echo_fb * (0.85f / 127.0f);
}
