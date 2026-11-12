/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* LEDGER-4's screen: the header, the page (240 x 200), the knobs' line. Numbers are hex, as trackers write them; the
 * line under each grid says in words what the cell under the cursor does. */

#define ROW_H 11
#define GRID_Y 2
static const uint16_t TRK_C[NTRK] = {RGB(255, 120, 90), RGB(110, 200, 255), RGB(255, 210, 90), RGB(170, 140, 255)};

static void cell_text(int32_t x, int32_t y, const char *s, int cur, int edit_on, uint16_t c)
{
    if (cur) {
        cv_rect(x - 2, y, text_w(&FONT_XS, s) + 4, ROW_H + 1, edit_on ? ACCENT : K_DIM);
        cv_text(x, y - 2, &FONT_XS, s, K_BG);
    } else {
        cv_text(x, y - 2, &FONT_XS, s, c);
    }
}
static void hx(uint8_t v, char *t)
{
    if (v == EMPTY) {
        t[0] = t[1] = '-';
        t[2] = 0;
    } else {
        hex2(v, t);
    }
}

static void draw_song(void)
{
    uint32_t first = U.srow > 13u ? U.srow - 13u : 0u, r, t;
    char s[8];
    if (first + 16u > NSONG)
        first = NSONG - 16u;
    for (t = 0; t < NTRK; t++) {
        cv_text(40 + (int32_t)t * 30, 0, &FONT_T, (P.trk_mute >> t & 1u) ? "MUTE" : t == 0 ? "T1" : t == 1 ? "T2" : t == 2 ? "T3" : "T4",
                (P.trk_mute >> t & 1u) ? K_REC : TRK_C[t]);
    }
    for (r = first; r < first + 16u; r++) {
        int32_t y = 11 + (int32_t)(r - first) * ROW_H;
        hex2(r, s);
        cv_text(6, y - 2, &FONT_XS, s, r % 4u ? K_FAINT : K_DIM);
        for (t = 0; t < NTRK; t++) {
            int playing = T.playing && E.mode == PM_SONG && E.t[t].active && E.t[t].srow == r;
            hx(P.song[r][t], s);
            if (playing)
                cv_rect(36 + (int32_t)t * 30, y + 1, 3, ROW_H - 2, TRK_C[t]);
            cell_text(42 + (int32_t)t * 30, y, s, r == U.srow && t == U.strk, U.edit,
                      P.song[r][t] == EMPTY ? K_FAINT : (P.trk_mute >> t & 1u) ? K_DIM : K_FG);
        }
    }
    /* the right: what each track plays now */
    cv_text(168, 0, &FONT_T, "NOW", K_DIM);
    for (t = 0; t < NTRK; t++) {
        int32_t y = 11 + (int32_t)t * 40;
        play_t *p = &E.t[t];
        cv_rect(164, y, 72, 36, K_PANEL);
        cv_rect(164, y, 3, 36, TRK_C[t]);
        if (T.playing && p->active) {
            char a[16] = "CH ";
            hx(p->chain, s);
            s_cat(a, s);
            s_cat(a, " PH ");
            hx(p->phrase, s);
            s_cat(a, s);
            cv_text(170, y + 1, &FONT_T, a, K_FG);
            bar(170, y + 18, 60, 3, p->prow + 1, NROW, TRK_C[t]);
            if (p->sounding >= 0) {
                note_text(p->sounding, a);
                cv_text(170, y + 22, &FONT_T, a, heard[t] ? TRK_C[t] : K_DIM);
            }
        } else {
            cv_text(170, y + 1, &FONT_T, "--", K_FAINT);
        }
    }
}

static void draw_chain(void)
{
    uint32_t r;
    char s[12], t[40];
    const chain_t *c = &P.chn[U.chain];
    s_cpy(t, "CHAIN ");
    hex2(U.chain, s);
    s_cat(t, s);
    cv_text(6, 0, &FONT_T, t, ACCENT);
    
    for (r = 0; r < NROW; r++) {
        int32_t y = 11 + (int32_t)r * ROW_H;
        int playing = T.playing && E.mode != PM_PHRASE && E.t[U.strk].active && E.t[U.strk].chain == U.chain &&
                      E.t[U.strk].crow == r;
        hex2(r, s);
        cv_text(6, y - 2, &FONT_XS, s, r % 4u ? K_FAINT : K_DIM);
        if (playing)
            cv_rect(26, y + 1, 3, ROW_H - 2, ACCENT);
        hx(c->phr[r], s);
        cell_text(34, y, s, r == U.crow && U.ccol == 0u, U.edit, c->phr[r] == EMPTY ? K_FAINT : K_FG);
        s[0] = 0;
        if (c->phr[r] != EMPTY || c->tsp[r])
            sgn_cat(s, c->tsp[r]);
        else
            s_cpy(s, "--");
        cell_text(64, y, s, r == U.crow && U.ccol == 1u, U.edit, c->tsp[r] ? K_FG : K_FAINT);
    }
    /* the right: the phrase under the cursor, in small */
    if (c->phr[U.crow] != EMPTY) {
        const phrase_t *ph = &P.phr[c->phr[U.crow]];
        s_cpy(t, "PHRASE ");
        hex2(c->phr[U.crow], s);
        s_cat(t, s);
        cv_text(112, 0, &FONT_T, t, K_DIM);
        for (r = 0; r < NROW; r++) {
            int32_t y = 11 + (int32_t)r * ROW_H;
            const row_t *w = &ph->r[r];
            if (w->note == NOTE_OFF)
                s_cpy(s, "OFF");
            else if (w->note)
                note_text(w->note, s);
            else
                s_cpy(s, "---");
            cv_text(112, y - 1, &FONT_T, s, w->note ? K_FG : K_FAINT);
            if (w->fx) {
                s[0] = FX_CH[w->fx % NFX];
                s[1] = 0;
                cv_text(150, y - 1, &FONT_T, s, ACCENT);
                hex2(w->val, s);
                cv_text(158, y - 1, &FONT_T, s, K_DIM);
            }
        }
    }
}

static void draw_phrase(void)
{
    static const int32_t COLX[NPC] = {28, 66, 92, 118, 132};
    uint32_t r;
    char s[12], t[48];
    const phrase_t *ph = &P.phr[U.phrase];
    const play_t *pl = &E.t[U.strk];
    s_cpy(t, "PHRASE ");
    hex2(U.phrase, s);
    s_cat(t, s);
    cv_text(6, 0, &FONT_T, t, ACCENT);
    cv_text(66, 0, &FONT_T, "IN", K_DIM);
    cv_text(92, 0, &FONT_T, "VL", K_DIM);
    cv_text(118, 0, &FONT_T, "CMD", K_DIM);
    for (r = 0; r < NROW; r++) {
        int32_t y = 11 + (int32_t)r * ROW_H;
        const row_t *w = &ph->r[r];
        int playing = 0, c;
        uint32_t tt;
        for (tt = 0; tt < NTRK; tt++)
            if (T.playing && E.t[tt].active && E.t[tt].phrase == U.phrase && E.t[tt].prow == r)
                playing = 1;
        hex2(r, s);
        cv_text(6, y - 2, &FONT_XS, s, r % 4u ? K_FAINT : K_DIM);
        if (playing)
            cv_rect(22, y + 1, 3, ROW_H - 2, ACCENT);
        for (c = 0; c < (int)NPC; c++) {
            uint16_t col = K_FG;
            switch (c) {
            case PC_NOTE:
                if (w->note == NOTE_OFF)
                    s_cpy(s, "OFF");
                else if (w->note)
                    note_text(w->note, s);
                else {
                    s_cpy(s, "---");
                    col = K_FAINT;
                }
                break;
            case PC_INS: hx(w->ins, s); col = w->ins == EMPTY ? K_FAINT : K_FG; break;
            case PC_VEL: hx(w->vel, s); col = w->vel == EMPTY ? K_FAINT : K_DIM; break;
            case PC_FX: s[0] = FX_CH[w->fx % NFX]; s[1] = 0; col = w->fx ? ACCENT : K_FAINT; break;
            default: hex2(w->val, s); col = w->fx ? K_FG : K_FAINT; break;
            }
            cell_text(COLX[c], y, s, r == U.prow && c == (int)U.pcol, U.edit, col);
        }
    }
    /* the right: the instrument of the row, and the keys' octave */
    {
        uint32_t in = ph->r[U.prow].ins != EMPTY ? ph->r[U.prow].ins : U.ins;
        const patch_t *p = &P.ins[in % NINS];
        cv_rect(164, 12, 72, 92, K_PANEL);
        s_cpy(t, "INS ");
        hex2(in, s);
        s_cat(t, s);
        cv_text(170, 12, &FONT_T, t, ACCENT);
        cv_text(170, 26, &FONT_T, p->type == PT_DRUM ? "DRUMS" : p->type == PT_MIDI ? "MIDI ONLY" : WAVE_NM[p->wave % NWAVE], K_FG);
        cv_text(170, 42, &FONT_T, "CUT", K_DIM);
        bar(196, 47, 34, 3, p->cutoff, 127, ACCENT);
        cv_text(170, 56, &FONT_T, "DEC", K_DIM);
        bar(196, 61, 34, 3, p->decay, 127, ACCENT);
        s_cpy(t, "OCT ");
        sgn_cat(t, U.oct);
        cv_text(170, 72, &FONT_T, t, K_DIM);
        s_cpy(t, "STEP ");
        i_cat(t, U.step);
        cv_text(170, 86, &FONT_T, t, K_DIM);
        if (T.playing && pl->active && pl->sounding >= 0) {
            note_text(pl->sounding, s);
            cv_text(170, 108, &FONT_B, s, TRK_C[U.strk % NTRK]);
        }
        cv_text(170, 130, &FONT_T, U.edit ? "EDIT" : "LISTEN", U.edit ? K_REC : K_DIM);
    }
}

static void draw_list_page(const row_def_t *rows, uint32_t n, uint32_t cur, const char *title)
{
    cv_text(6, 0, &FONT_T, title, ACCENT);
    kit_list_draw(rows, n, cur, 14, 172, ACCENT);
}

static void draw_footline(void)                         /* the explanation line at the page's foot */
{
    char t[48], s[8];
    t[0] = 0;
    if (U.page == PG_PHRASE) {
        const row_t *w = cur_row();
        if (U.pcol >= PC_FX)
            fx_explain(w->fx, w->val, t);
        else if (U.pcol == PC_NOTE)
            s_cpy(t, U.edit ? "KEYS WRITE  ARP: NOTE OFF  FX: CLEAR" : "EDIT OFF: KEYS LISTEN (REC)");
        else
            s_cpy(t, "WHITE KEYS TYPE HEX 0..F");
    } else if (U.page == PG_SONG) {
        s_cpy(t, "SEL: OPEN CHAIN  ARP: NEW  BLACK 1-4: MUTE");
    } else if (U.page == PG_CHAIN) {
        s_cpy(t, "SEL: OPEN PHRASE  ARP: NEW  K2: TRANSPOSE");
    } else if (U.page == PG_INSTR) {
        s_cpy(t, "INSTRUMENT ");
        hex2(U.ins, s);
        s_cat(t, s);
        s_cat(t, "  ALGO: ANOTHER  KEYS: PLAY IT");
    }
    cv_rect(0, 187, 240, 1, K_LINE);
    cv_text(6, 188, &FONT_T, t, K_DIM);
}

static void draw(void)
{
    kit_header("LEDGER-4", PG_NM[U.page], ACCENT, T.playing, U.edit && U.page == PG_PHRASE, T.bpm10);
    cv_begin(240, 200, K_BG);
    if (K.card < NB && CARD_TEXT[K.card]) {
        kit_card(K.card, CARD_TEXT[K.card], 200, ACCENT);
    } else {
        switch (U.page) {
        case PG_SONG: draw_song(); break;
        case PG_CHAIN: draw_chain(); break;
        case PG_PHRASE: draw_phrase(); break;
        case PG_INSTR: draw_list_page(INS_ROWS, NINS_ROWS, U.irow, PT_TYPE_NM[CUR_INS->type % NPT_TYPE]); break;
        default: draw_list_page(PROJ_ROWS, NPROJ_ROWS, U.grow, "PROJECT"); break;
        }
        draw_footline();
    }
    cv_blit(0, 22);
    if (U.page == PG_PHRASE)
        kit_knobs("VALUE x16", "CUTOFF", "DECAY", "TEMPO", ACCENT);
    else if (U.page == PG_CHAIN)
        kit_knobs("VALUE x16", "TRANSPOSE", "", "TEMPO", ACCENT);
    else if (U.page == PG_SONG)
        kit_knobs("VALUE x16", "", "", "TEMPO", ACCENT);
    else
        kit_knobs("VALUE", "", "", "", ACCENT);
}

static void lights(void)
{
    static const uint8_t PAGE_B[NPG] = {B_HOME, B_SEQ, B_EDIT, B_ENV, B_GLO};
    uint32_t btn = 1u << PAGE_B[U.page], keys = K.keys, dkeys = 0, t;
    if (T.playing)
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    if (U.edit)
        btn |= 1u << B_REC;
    if (U.dirty)
        btn |= 1u << B_SAVE;
    if (U.page == PG_SONG)                              /* black keys 1-4: lit while the track plays */
        for (t = 0; t < NTRK; t++)
            if (!(P.trk_mute >> t & 1u))
                dkeys |= 1u << BKEY[t];
    plat_lights(btn, keys, 0, dkeys, 1);
}
