/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* LEDGER-4's pages and controls.
 *   HOME song   SEQ chain   EDIT phrase   ENV instrument   GLO project
 *   SELECT the row, ALGO the column, PRESETS the value (K1 by 16, or an octave), SEL (a tap) goes into what is under
 *   the cursor, ARP a new one (or a note off), FX clears, SEL + LFO copies, LFO pastes, REC writes or only listens.
 *   The keys write notes in the note column; in a number column the 16 white keys are the hex digits 0..F, typed
 *   high digit first, and in the command column they pick the command. */

#define ACCENT RGB(255, 196, 64)
enum { PG_SONG, PG_CHAIN, PG_PHRASE, PG_INSTR, PG_PROJECT, NPG };
static const char *const PG_NM[NPG] = {"SONG", "CHAIN", "PHRASE", "INSTRUMENT", "PROJECT"};
enum { PC_NOTE, PC_INS, PC_VEL, PC_FX, PC_VAL, NPC };

#define SET_MAGIC 0x3153474Cu
typedef struct {
    uint32_t magic;
    uint8_t slot, ble_off, rsv[2];
} settings_t;
static settings_t set;

static struct {
    uint8_t page;
    uint8_t srow, strk;
    uint8_t chain, crow, ccol;
    uint8_t phrase, prow, pcol;
    uint8_t ins, irow, grow;
    uint8_t edit, step, digits;
    int8_t oct;
    uint8_t dirty, save_wait;
    uint8_t clip_kind;                                  /* 0 none, 1 phrase, 2 chain */
    phrase_t clip_ph;
    chain_t clip_ch;
    int16_t aud_note;                                   /* the key auditioned (-1 none) */
} U;

static const char *const CARD_TEXT[NB] = {
    "FX clears what is under the cursor: a note, a number, a command.",
    ("SEL tapped goes into what is under the cursor: a song cell opens its chain, a chain row its phrase, a phrase's "
     "instrument its sound. Held: SEL + LFO copies the chain or phrase, SEL + PRESETS transposes the phrase."),
    "ENV: the INSTRUMENT page. ALGO picks one of 16, SELECT a setting, PRESETS changes it.",
    "LFO pastes the chain or phrase copied with SEL + LFO over the one shown.",
    "EDIT: the PHRASE page, 16 rows of note, instrument, velocity, command and value.",
    "GLO: the PROJECT page. Tempo, groove (swing), clock, echo, and the four project slots.",
    "HOME: the SONG page. 64 rows, a chain for each of the 4 tracks. Black keys 1-4 mute the tracks here.",
    "SAVE writes the project to its slot (GLO picks the slot). While playing it saves at the stop.",
    "ARP gives the cell the next unused chain, phrase or instrument. In the note column it writes a note off.",
    "SEQ: the CHAIN page, 16 rows of phrase and transpose.",
    "PLAY starts and stops. On SONG it plays the song from the cursor's row; on CHAIN and PHRASE it loops what is shown.",
    "REC: edit on (lit) or off. Off, the keys only play the instrument; nothing is written.",
    "OCT- lowers the keys an octave.",
    "OCT+ raises the keys an octave.",
#if PLAT_SMK37
    0, 0, 0, 0,
#endif
};

static const char *const HINTS[NB] = {
    [B_SEL] = "LFO COPY  PRESETS TRANSPOSE  TAP: OPEN",
    [B_FX] = "CLEARS THE CELL", [B_ARP] = "NEW CHAIN / PHRASE / NOTE OFF", [B_LFO] = "PASTE  (SEL+LFO COPY)",
};

static void dirty(void) { U.dirty = 1; }

/* ---- the next unused chain or phrase ---- */
static int phrase_used(uint32_t ph)
{
    uint32_t c, r;
    for (r = 0; r < NROW; r++)
        if (P.phr[ph].r[r].note || P.phr[ph].r[r].fx)
            return 1;
    for (c = 0; c < NCHN; c++)
        for (r = 0; r < NROW; r++)
            if (P.chn[c].phr[r] == ph)
                return 1;
    return 0;
}
static int chain_used(uint32_t ch)
{
    uint32_t s, t;
    if (chain_first_row(ch) >= 0)
        return 1;
    for (s = 0; s < NSONG; s++)
        for (t = 0; t < NTRK; t++)
            if (P.song[s][t] == ch)
                return 1;
    return 0;
}
static uint32_t phrase_free(void)
{
    uint32_t i;
    for (i = 0; i < NPHR; i++)
        if (!phrase_used(i))
            return i;
    return EMPTY;
}
static uint32_t chain_free(void)
{
    uint32_t i;
    for (i = 0; i < NCHN; i++)
        if (!chain_used(i))
            return i;
    return EMPTY;
}

/* a number cell: up and down, wrapping, EMPTY below 0 */
static uint8_t num_step(uint8_t v, int32_t d, uint32_t n)
{
    int32_t x = v == EMPTY ? -1 : (int32_t)v;
    x += d;
    if (x < -1)
        x = -1;
    if (x >= (int32_t)n)
        x = (int32_t)n - 1;
    return x < 0 ? EMPTY : (uint8_t)x;
}

/* ---- the explanation line: the command under the cursor in words ---- */
static void fx_explain(uint32_t fx, uint32_t v, char *t)
{
    uint32_t x = v >> 4, y = v & 15u;
    s_cpy(t, FX_NM[fx]);
    s_cat(t, "  ");
    switch (fx) {
    case FX_ARP: s_cat(t, "+0 +"); i_cat(t, (int32_t)x); s_cat(t, " +"); i_cat(t, (int32_t)y); break;
    case FX_CHANCE: i_cat(t, (int32_t)(v > 100u ? 100u : v)); s_cat(t, "% TO PLAY"); break;
    case FX_DELAY: s_cat(t, "WAIT "); i_cat(t, (int32_t)v); s_cat(t, " TICKS"); break;
    case FX_KILL: s_cat(t, "CUT AFTER "); i_cat(t, (int32_t)v); s_cat(t, " TICKS"); break;
    case FX_RETRIG: s_cat(t, "EVERY "); i_cat(t, (int32_t)(x ? x : 1u)); s_cat(t, " VOL "); sgn_cat(t, (int32_t)y - 8); break;
    case FX_SLIDE: s_cat(t, "GLIDE TIME "); i_cat(t, (int32_t)v); break;
    case FX_TEMPO: if (v >= 30u) { i_cat(t, (int32_t)v); s_cat(t, " BPM"); } else s_cat(t, "(30+ BPM)"); break;
    case FX_HOP: s_cat(t, "NEXT AT ROW "); i_cat(t, (int32_t)y); break;
    case FX_FILTER: s_cat(t, "CUTOFF "); i_cat(t, (int32_t)(v / 2u)); break;
    case FX_MIDICC: s_cat(t, "CC VALUE "); i_cat(t, (int32_t)(v / 2u)); break;
    case FX_RANDOM: s_cat(t, "+0..+"); i_cat(t, (int32_t)v); s_cat(t, " SEMIS"); break;
    case FX_GROOVE: i_cat(t, (int32_t)clamp((int32_t)x, 1, 15)); s_cat(t, "/"); i_cat(t, (int32_t)clamp((int32_t)y, 1, 15)); break;
    case FX_ECHO: s_cat(t, "SEND "); i_cat(t, (int32_t)(v / 2u)); break;
    default: t[0] = 0; s_cpy(t, "KEYS: WHITE PICKS A COMMAND"); break;
    }
}

/* ---- settings and the project's slots ---- */
static void settings_save(void) { plat_store_save(OBJ_SETTINGS, &set, sizeof set); }
int ledger_ble_off(void) { return set.ble_off; }

static void project_save_now(void)
{
    int r;
    P.bpm10 = T.bpm10;
    r = slot_save(set.slot, PROJ_MAGIC, PROJ_VER, &P, sizeof P);
    if (r == 0) {
        U.dirty = 0;
        app_say("SAVED", set.slot == 0 ? "SLOT 1" : set.slot == 1 ? "SLOT 2" : set.slot == 2 ? "SLOT 3" : "SLOT 4");
    } else {
        app_say("SAVE", r == -1 ? "TOO BIG" : "FAILED");
    }
}
static void project_save(void)
{
    if (T.playing) {
        U.save_wait = 1;
        app_say("SAVES", "AT THE STOP");
    } else {
        project_save_now();
    }
}
static void transport_wait_stop(void)
{
    uint32_t t0 = plat_ms();
    T.want = 2;
    while (T.playing && plat_ms() - t0 < 200u)
        plat_yield();
}
static int project_load(uint32_t slot)
{
    if (T.playing)
        transport_wait_stop();
    if (!slot_load(slot, PROJ_MAGIC, &P, sizeof P))
        return 0;
    project_apply();
    return 1;
}

/* ---- the instrument page's rows ---- */
#define CUR_INS (&P.ins[U.ins % NINS])
static void v_u8(char *o, uint32_t v) { i_text((int32_t)v, o); }
static void iv_type(char *o) { s_cpy(o, PT_TYPE_NM[CUR_INS->type % NPT_TYPE]); }
static void iv_wave(char *o) { s_cpy(o, CUR_INS->type == PT_DRUM ? "(NOTES PICK)" : WAVE_NM[CUR_INS->wave % NWAVE]); }
static void iv_cut(char *o) { v_u8(o, CUR_INS->cutoff); }
static void iv_res(char *o) { v_u8(o, CUR_INS->reso); }
static void iv_env(char *o) { v_u8(o, CUR_INS->envmod); }
static void iv_att(char *o) { v_u8(o, CUR_INS->attack); }
static void iv_dec(char *o) { v_u8(o, CUR_INS->decay); }
static void iv_sus(char *o) { v_u8(o, CUR_INS->sustain); }
static void iv_rel(char *o) { v_u8(o, CUR_INS->release); }
static void iv_lvl(char *o) { v_u8(o, CUR_INS->level); }
static void iv_det(char *o) { v_u8(o, CUR_INS->detune); }
static void iv_echo(char *o) { v_u8(o, CUR_INS->echo); }
static void iv_oct(char *o) { o[0] = 0; sgn_cat(o, CUR_INS->octave); }
static void iv_chan(char *o) { if (CUR_INS->chan) v_u8(o, CUR_INS->chan); else s_cpy(o, "OFF"); }
static void iv_cc(char *o) { v_u8(o, CUR_INS->cc); }
static void ic(uint8_t *f, int32_t d, int32_t lo, int32_t hi) { *f = (uint8_t)clamp((int32_t)*f + d, lo, hi); dirty(); }
static void ic_type(int32_t d) { ic(&CUR_INS->type, d, 0, NPT_TYPE - 1); }
static void ic_wave(int32_t d) { ic(&CUR_INS->wave, d, 0, NWAVE - 1); }
static void ic_cut(int32_t d) { ic(&CUR_INS->cutoff, d, 0, 127); }
static void ic_res(int32_t d) { ic(&CUR_INS->reso, d, 0, 127); }
static void ic_env(int32_t d) { ic(&CUR_INS->envmod, d, 0, 127); }
static void ic_att(int32_t d) { ic(&CUR_INS->attack, d, 0, 127); }
static void ic_dec(int32_t d) { ic(&CUR_INS->decay, d, 0, 127); }
static void ic_sus(int32_t d) { ic(&CUR_INS->sustain, d, 0, 127); }
static void ic_rel(int32_t d) { ic(&CUR_INS->release, d, 0, 127); }
static void ic_lvl(int32_t d) { ic(&CUR_INS->level, d, 0, 127); }
static void ic_det(int32_t d) { ic(&CUR_INS->detune, d, 0, 127); }
static void ic_echo(int32_t d) { ic(&CUR_INS->echo, d, 0, 127); }
static void ic_oct(int32_t d) { CUR_INS->octave = (int8_t)clamp(CUR_INS->octave + d, -3, 3); dirty(); }
static void ic_chan(int32_t d) { ic(&CUR_INS->chan, d, 0, 16); }
static void ic_cc(int32_t d) { ic(&CUR_INS->cc, d, 0, 127); }
static const row_def_t INS_ROWS[] = {
    {"TYPE", iv_type, ic_type, 0},      {"WAVE", iv_wave, ic_wave, 0},       {"CUTOFF", iv_cut, ic_cut, 0},
    {"RESONANCE", iv_res, ic_res, 0},   {"ENV > FILTER", iv_env, ic_env, 0}, {"ATTACK", iv_att, ic_att, 0},
    {"DECAY", iv_dec, ic_dec, 0},       {"SUSTAIN", iv_sus, ic_sus, 0},      {"RELEASE", iv_rel, ic_rel, 0},
    {"LEVEL", iv_lvl, ic_lvl, 0},       {"DETUNE", iv_det, ic_det, 0},       {"ECHO SEND", iv_echo, ic_echo, 0},
    {"OCTAVE", iv_oct, ic_oct, 0},      {"MIDI CHANNEL", iv_chan, ic_chan, 0}, {"M COMMAND CC", iv_cc, ic_cc, 0},
};
#define NINS_ROWS (sizeof INS_ROWS / sizeof INS_ROWS[0])

/* ---- the project page's rows ---- */
static void pv_tempo(char *o) { o[0] = 0; i_cat(o, T.bpm10 / 10); s_cat(o, "."); i_cat(o, T.bpm10 % 10); }
static void pv_g0(char *o) { v_u8(o, P.groove[0]); }
static void pv_g1(char *o) { v_u8(o, P.groove[1]); }
static void pv_clk(char *o) { s_cpy(o, CK_NM[T.clk_mode % NCK]); }
static void pv_clko(char *o) { s_cpy(o, T.clk_send ? "SEND" : "OFF"); }
static void pv_et(char *o) { v_u8(o, P.echo_time); }
static void pv_ef(char *o) { v_u8(o, P.echo_fb); }
static void pv_slot(char *o)
{
    o[0] = 0;
    i_cat(o, set.slot + 1);
    s_cat(o, slot_used(set.slot, PROJ_MAGIC) ? "  (SAVED)" : "  (EMPTY)");
}
static void pv_ble(char *o) { s_cpy(o, set.ble_off ? "OFF" : "ON"); }
static void pc_tempo(int32_t d) { T.bpm10 = (uint16_t)clamp((int32_t)T.bpm10 + d * 10, 300, 3000); P.bpm10 = T.bpm10; dirty(); }
static void pc_g0(int32_t d) { ic(&P.groove[0], d, 1, 15); }
static void pc_g1(int32_t d) { ic(&P.groove[1], d, 1, 15); }
static void pc_clk(int32_t d) { T.clk_mode = (uint8_t)clamp(T.clk_mode + d, 0, NCK - 1); P.clk_mode = T.clk_mode; dirty(); }
static void pc_clko(int32_t d) { T.clk_send = d > 0; P.clk_send = T.clk_send; dirty(); }
static void pc_et(int32_t d) { ic(&P.echo_time, d, 0, 127); project_apply(); }
static void pc_ef(int32_t d) { ic(&P.echo_fb, d, 0, 127); project_apply(); }
static void pc_slot(int32_t d) { set.slot = (uint8_t)clamp(set.slot + d, 0, NSLOTS - 1); settings_save(); }
static void pa_save(void) { project_save(); }
static void pa_load(void)
{
    if (project_load(set.slot)) {
        U.dirty = 0;
        app_say("LOADED", "");
    } else {
        app_say("SLOT", "EMPTY");
    }
}
static void pa_clear(void) { slot_clear(set.slot); app_say("SLOT", "CLEARED"); }
static void pa_new(void)
{
    if (T.playing)
        transport_wait_stop();
    project_new();
    project_apply();
    U.dirty = 1;
    app_say("NEW", "PROJECT");
}
static void pc_ble(int32_t d)
{
    set.ble_off = d < 0;
    plat_ble_enable(!set.ble_off);
    settings_save();
}
static const row_def_t PROJ_ROWS[] = {
    {"TEMPO", pv_tempo, pc_tempo, 0},       {"GROOVE EVEN ROWS", pv_g0, pc_g0, 0}, {"GROOVE ODD ROWS", pv_g1, pc_g1, 0},
    {"CLOCK", pv_clk, pc_clk, 0},           {"CLOCK OUT", pv_clko, pc_clko, 0},   {"ECHO TIME", pv_et, pc_et, 0},
    {"ECHO FEEDBACK", pv_ef, pc_ef, 0},     {"SLOT", pv_slot, pc_slot, 0},        {"SAVE TO SLOT", 0, 0, pa_save},
    {"LOAD SLOT", 0, 0, pa_load},           {"CLEAR SLOT", 0, 0, pa_clear},       {"NEW PROJECT", 0, 0, pa_new},
    {"BLUETOOTH", pv_ble, pc_ble, 0},
};
#define NPROJ_ROWS (sizeof PROJ_ROWS / sizeof PROJ_ROWS[0])

/* ---- editing ---- */
static row_t *cur_row(void) { return &P.phr[U.phrase].r[U.prow]; }

static void cursor_moved(void) { U.digits = 0; }

static void phrase_advance(void)
{
    U.prow = (uint8_t)((U.prow + U.step) % NROW);
    cursor_moved();
}

static uint8_t hex_type(uint8_t v, uint32_t k)         /* a hex digit typed into a byte: high digit first */
{
    uint8_t r = U.digits == 0u || v == EMPTY ? (uint8_t)k : (uint8_t)((v << 4 | k) & 0xFFu);
    U.digits++;
    return r;
}

/* a white key (k 0..15) on a number column */
static void hex_key(uint32_t k)
{
    switch (U.page) {
    case PG_SONG:
        P.song[U.srow][U.strk] = hex_type(P.song[U.srow][U.strk], k) % NCHN;
        if (U.digits >= 2u) {
            U.srow = (uint8_t)((U.srow + 1u) % NSONG);
            cursor_moved();
        }
        break;
    case PG_CHAIN:
        if (U.ccol == 0u)
            P.chn[U.chain].phr[U.crow] = hex_type(P.chn[U.chain].phr[U.crow], k) % NPHR;
        else
            P.chn[U.chain].tsp[U.crow] = (int8_t)hex_type((uint8_t)P.chn[U.chain].tsp[U.crow], k);
        if (U.digits >= 2u) {
            U.crow = (uint8_t)((U.crow + 1u) % NROW);
            cursor_moved();
        }
        break;
    case PG_PHRASE: {
        row_t *r = cur_row();
        if (U.pcol == PC_FX) {                          /* the key is the command; then on to its value */
            if (k < NFX) {
                r->fx = (uint8_t)k;
                U.pcol = PC_VAL;
                cursor_moved();
            }
            break;
        }
        if (U.pcol == PC_INS) {
            r->ins = hex_type(r->ins, k) % NINS;
            U.ins = r->ins;
        } else if (U.pcol == PC_VEL) {
            r->vel = (uint8_t)(hex_type(r->vel, k) & 0x7Fu);
        } else if (U.pcol == PC_VAL) {
            r->val = hex_type(r->val, k);
        }
        if (U.digits >= 2u)
            phrase_advance();
        break;
    }
    default: break;
    }
    dirty();
}

static void note_key(uint32_t key, uint32_t vel)       /* a key on the note column (or anywhere, edit off) */
{
    int32_t n = 53 + (int32_t)key + 12 * U.oct;
    row_t *r = cur_row();
    if (U.aud_note >= 0)
        live_note(6, &P.ins[U.ins % NINS], U.aud_note, 0, 0);
    live_note(6, &P.ins[U.ins % NINS], n, vel, 1);
    U.aud_note = (int16_t)n;
    if (!U.edit || U.page != PG_PHRASE || U.pcol != PC_NOTE)
        return;
    r->note = (uint8_t)clamp(n, 1, 127);
    if (r->ins == EMPTY)
        r->ins = U.ins;
    else
        U.ins = r->ins;
    dirty();
    phrase_advance();
}

static void clear_cell(void)
{
    switch (U.page) {
    case PG_SONG: P.song[U.srow][U.strk] = EMPTY; break;
    case PG_CHAIN:
        if (U.ccol == 0u)
            P.chn[U.chain].phr[U.crow] = EMPTY;
        else
            P.chn[U.chain].tsp[U.crow] = 0;
        break;
    case PG_PHRASE: {
        row_t *r = cur_row();
        if (U.pcol == PC_NOTE) {                        /* the note column clears the whole row */
            r->note = 0;
            r->ins = EMPTY;
            r->vel = EMPTY;
        } else if (U.pcol == PC_INS) {
            r->ins = EMPTY;
        } else if (U.pcol == PC_VEL) {
            r->vel = EMPTY;
        } else {
            r->fx = FX_NONE;
            r->val = 0;
        }
        break;
    }
    default: return;
    }
    cursor_moved();
    dirty();
}

static void new_here(void)                              /* ARP */
{
    uint32_t n;
    switch (U.page) {
    case PG_SONG:
        if ((n = chain_free()) != EMPTY) {
            P.song[U.srow][U.strk] = (uint8_t)n;
            dirty();
        }
        break;
    case PG_CHAIN:
        if (U.ccol == 0u && (n = phrase_free()) != EMPTY) {
            P.chn[U.chain].phr[U.crow] = (uint8_t)n;
            dirty();
        }
        break;
    case PG_PHRASE:
        if (U.pcol == PC_NOTE) {
            cur_row()->note = NOTE_OFF;
            phrase_advance();
        } else if (U.pcol == PC_INS) {
            cur_row()->ins = (uint8_t)((U.ins + 1u) % NINS);
            U.ins = cur_row()->ins;
        }
        dirty();
        break;
    default: break;
    }
}

static void dive(void)                                  /* SEL tapped: into what is under the cursor */
{
    if (U.page == PG_SONG) {
        uint8_t c = P.song[U.srow][U.strk];
        if (c == EMPTY) {
            c = (uint8_t)chain_free();
            if (c == EMPTY)
                return;
            P.song[U.srow][U.strk] = c;
            dirty();
        }
        U.chain = c;
        U.page = PG_CHAIN;
    } else if (U.page == PG_CHAIN) {
        uint8_t p = P.chn[U.chain].phr[U.crow];
        if (p == EMPTY) {
            p = (uint8_t)phrase_free();
            if (p == EMPTY)
                return;
            P.chn[U.chain].phr[U.crow] = p;
            dirty();
        }
        U.phrase = p;
        U.page = PG_PHRASE;
    } else if (U.page == PG_PHRASE) {
        if (cur_row()->ins != EMPTY)
            U.ins = cur_row()->ins;
        U.page = PG_INSTR;
    }
    cursor_moved();
}

static void value_turn(int32_t d, int big)              /* PRESETS (or K1, by 16 or an octave) on the cell */
{
    switch (U.page) {
    case PG_SONG:
        P.song[U.srow][U.strk] = num_step(P.song[U.srow][U.strk], big ? d * 16 : d, NCHN);
        break;
    case PG_CHAIN:
        if (U.ccol == 0u)
            P.chn[U.chain].phr[U.crow] = num_step(P.chn[U.chain].phr[U.crow], big ? d * 16 : d, NPHR);
        else
            P.chn[U.chain].tsp[U.crow] = (int8_t)clamp(P.chn[U.chain].tsp[U.crow] + (big ? d * 12 : d), -48, 48);
        break;
    case PG_PHRASE: {
        row_t *r = cur_row();
        switch (U.pcol) {
        case PC_NOTE:
            if (r->note == 0u || r->note == NOTE_OFF)
                r->note = (uint8_t)(r->note ? 60 : 60);
            else
                r->note = (uint8_t)clamp(r->note + (big ? d * 12 : d), 1, 127);
            if (r->ins == EMPTY)
                r->ins = U.ins;
            break;
        case PC_INS: r->ins = num_step(r->ins, d, NINS); if (r->ins != EMPTY) U.ins = r->ins; break;
        case PC_VEL: r->vel = r->vel == EMPTY ? 100 : (uint8_t)clamp(r->vel + (big ? d * 16 : d), 1, 127); break;
        case PC_FX: r->fx = (uint8_t)clamp(r->fx + d, 0, NFX - 1); break;
        default: r->val = (uint8_t)clamp(r->val + (big ? d * 16 : d), 0, 255); break;
        }
        break;
    }
    default: return;
    }
    cursor_moved();
    dirty();
}

static void play_here(void)                             /* PLAY: what is shown */
{
    if (T.playing) {
        T.want = 2;
        return;
    }
    if (U.page == PG_CHAIN) {
        E.mode = PM_CHAIN;
        E.solo_chain = U.chain;
        E.solo_track = U.strk;
    } else if (U.page == PG_PHRASE) {
        E.mode = PM_PHRASE;
        E.solo_phrase = U.phrase;
        E.solo_track = U.strk;
    } else {
        uint32_t t;
        E.mode = PM_SONG;
        for (t = 0; t < NTRK; t++)
            E.t[t].srow = U.srow;
    }
    if (T.clk_mode == CK_INTERNAL)
        T.want = 1;
    else
        app_say("WAITING", "FOR A CLOCK");
}

static void input(void)
{
    uint32_t i, k;
    kit_input();
    /* the pages */
    if (DOWN(B_HOME)) U.page = PG_SONG;
    if (DOWN(B_SEQ)) U.page = PG_CHAIN;
    if (DOWN(B_EDIT)) U.page = PG_PHRASE;
    if (DOWN(B_ENV)) U.page = PG_INSTR;
    if (DOWN(B_GLO)) U.page = PG_PROJECT;
    if (DOWN(B_PLAY)) play_here();
    if (DOWN(B_REC)) {
        U.edit = !U.edit;
        app_say("EDIT", U.edit ? "ON" : "OFF (LISTEN)");
    }
    if (DOWN(B_OCTDN) || DOWN(B_OCTUP)) {
        char t[8] = "";
        U.oct = (int8_t)clamp(U.oct + (DOWN(B_OCTUP) ? 1 : -1), -3, 3);
        sgn_cat(t, U.oct);
        app_say("OCTAVE", t);
    }
    if (DOWN(B_SAVE))
        project_save();
    if (HELD(B_SEL) && DOWN(B_LFO)) {                   /* copy */
        kit_use(B_SEL);
        if (U.page == PG_PHRASE) {
            U.clip_ph = P.phr[U.phrase];
            U.clip_kind = 1;
            app_say("COPIED", "PHRASE");
        } else if (U.page == PG_CHAIN) {
            U.clip_ch = P.chn[U.chain];
            U.clip_kind = 2;
            app_say("COPIED", "CHAIN");
        }
    } else if (DOWN(B_LFO)) {                           /* paste */
        if (U.page == PG_PHRASE && U.clip_kind == 1u) {
            P.phr[U.phrase] = U.clip_ph;
            dirty();
            app_say("PASTED", "PHRASE");
        } else if (U.page == PG_CHAIN && U.clip_kind == 2u) {
            P.chn[U.chain] = U.clip_ch;
            dirty();
            app_say("PASTED", "CHAIN");
        }
    }
    if (TAPPED(B_SEL) && U.page <= PG_PHRASE)
        dive();
    if (DOWN(B_FX))
        clear_cell();
    if (DOWN(B_ARP))
        new_here();

    /* the encoders */
    if (U.page == PG_INSTR) {
        U.ins = (uint8_t)clamp(U.ins + K.enc[EN_ALGO], 0, NINS - 1);
        kit_list_input(INS_ROWS, NINS_ROWS, &U.irow);
    } else if (U.page == PG_PROJECT) {
        kit_list_input(PROJ_ROWS, NPROJ_ROWS, &U.grow);
    } else {
        int32_t r = K.enc[EN_SELECT], c = K.enc[EN_ALGO];
        if (r || c) {
            cursor_moved();
            if (U.page == PG_SONG) {
                U.srow = (uint8_t)clamp(U.srow + r, 0, NSONG - 1);
                U.strk = (uint8_t)clamp(U.strk + c, 0, NTRK - 1);
            } else if (U.page == PG_CHAIN) {
                U.crow = (uint8_t)clamp(U.crow + r, 0, NROW - 1);
                U.ccol = (uint8_t)clamp(U.ccol + c, 0, 1);
            } else {
                U.prow = (uint8_t)clamp(U.prow + r, 0, NROW - 1);
                U.pcol = (uint8_t)clamp(U.pcol + c, 0, NPC - 1);
                if (r && !T.playing && cur_row()->note && cur_row()->note != NOTE_OFF) {   /* scrolling plays */
                    const patch_t *pa = &P.ins[(cur_row()->ins != EMPTY ? cur_row()->ins : U.ins) % NINS];
                    live_note(6, pa, cur_row()->note, cur_row()->vel != EMPTY ? cur_row()->vel : 100, 1);
                    live_note(6, pa, cur_row()->note, 0, 0);
                }
            }
        }
        if (K.enc[EN_PRESET] && HELD(B_SEL) && U.page == PG_PHRASE) {   /* transpose the phrase */
            kit_use(B_SEL);
            for (i = 0; i < NROW; i++) {
                row_t *w = &P.phr[U.phrase].r[i];
                if (w->note && w->note != NOTE_OFF)
                    w->note = (uint8_t)clamp(w->note + K.enc[EN_PRESET], 1, 127);
            }
            dirty();
        } else if (K.enc[EN_PRESET]) {
            value_turn(K.enc[EN_PRESET], 0);
        }
        if (K.enc[EN_K1])
            value_turn(K.enc[EN_K1], 1);
        if (U.page == PG_PHRASE) {                      /* K2..K3: the row's instrument, live */
            patch_t *p = &P.ins[(cur_row()->ins != EMPTY ? cur_row()->ins : U.ins) % NINS];
            if (K.enc[EN_K2]) { p->cutoff = (uint8_t)clamp(p->cutoff + K.enc[EN_K2] * 2, 0, 127); dirty(); }
            if (K.enc[EN_K3]) { p->decay = (uint8_t)clamp(p->decay + K.enc[EN_K3] * 2, 0, 127); dirty(); }
        } else if (U.page == PG_CHAIN) {
            if (K.enc[EN_K2]) {
                P.chn[U.chain].tsp[U.crow] = (int8_t)clamp(P.chn[U.chain].tsp[U.crow] + K.enc[EN_K2], -48, 48);
                dirty();
            }
        }
        if (K.enc[EN_K4])
            pc_tempo(K.enc[EN_K4]);
    }
    /* the keys */
    for (k = 0; k < NKEYS; k++) {
        int w = white_of(k), b = black_of(k);
        if (K.kdown >> k & 1u) {
            if (U.page == PG_SONG && b >= 0 && b < (int)NTRK) {
                P.trk_mute ^= (uint8_t)(1u << b);
                app_say(P.trk_mute >> b & 1u ? "MUTE" : "UNMUTE", b == 0 ? "T1" : b == 1 ? "T2" : b == 2 ? "T3" : "T4");
            } else if ((U.page == PG_PHRASE && U.pcol == PC_NOTE) || !U.edit || U.page >= PG_INSTR) {
                note_key(k, K.vel[k]);
            } else if (w >= 0) {
                hex_key((uint32_t)w);
            }
        }
        if ((K.kup >> k & 1u) && U.aud_note == 53 + (int32_t)k + 12 * U.oct) {
            live_note(6, &P.ins[U.ins % NINS], U.aud_note, 0, 0);
            U.aud_note = -1;
        }
    }
    if (U.page == PG_PHRASE && cur_row()->ins != EMPTY)
        midi_ins = cur_row()->ins;
    else
        midi_ins = U.ins;
    kit_cards(CARD_TEXT);
}
