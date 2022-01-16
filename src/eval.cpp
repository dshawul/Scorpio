#include "scorpio.h"
 
/* parameter */
#ifdef TUNE
#   define PARAM int
#else
#   define PARAM const int
#endif

/*
* Tunable parameters
*/
#define PARAMS_FILE "params.h"

#include PARAMS_FILE

/*material count*/
#define   MAX_MATERIAL    64

#ifdef TUNE
static int material = 0;
#endif

/*
static evaluator
*/
int SEARCHER::eval(bool skip_nn_l)
{
    int actual_score;
    bool use_nn_hard = (use_nn && !skip_nn && !skip_nn_l);

    /*phase of the game*/
    int phase = piece_c[white] + piece_c[black];
    phase = MIN(phase,MAX_MATERIAL);

#ifndef TUNE
    /* check_eval hash table */
    if(!use_nn || use_nn_hard) {
        if(probe_eval_hash(hash_key,actual_score))
            goto END_EVAL;
    }
#endif

    /*number of evaluation calls*/
    ecalls++;

    /* neural network evaluation */
    if(use_nn_hard) {
        actual_score = logit(probe_neural());
    }
    /*nnue evaluation*/
    else if(use_nnue) {
        int nnue_score;
        nnue_score = probe_nnue();
        nnue_score = (nnue_score * nnue_scale) / 128;
        nnue_score = (nnue_score * (720 + (phase * PAWN_MG) / 32)) / 1024;
        nnue_score += TEMPO_BONUS + (phase * TEMPO_SLOPE) / MAX_MATERIAL;
        actual_score = nnue_score;
    /*hand-crafted evaluation*/
    } else {
        actual_score = eval_hce();
    }
    /*special frc evals*/
    if(variant == 1) {
        actual_score += eval_frc_special();
    }
    /*scale some endgame evaluations*/
    if(!use_nn_hard && all_man_c <= 18) {
        int w_win_chance = 8,b_win_chance = 8;
        eval_win_chance(w_win_chance,b_win_chance);
        if(player == white) {
            if(actual_score > 0) {
                actual_score = (actual_score * w_win_chance) / 8;
            } else {
                actual_score = (actual_score * b_win_chance) / 8;
            }
        } else {
            if(actual_score > 0) {
                actual_score = (actual_score * b_win_chance) / 8;
            } else {
                actual_score = (actual_score * w_win_chance) / 8;
            }
        }
    }
    /*save evaluation in hash table*/
#ifndef TUNE
    if(!use_nn || use_nn_hard) {
        record_eval_hash(hash_key,actual_score);
    }
#endif

END_EVAL:
    /*scale by 50 move rule*/
    if(!use_nn_hard)
        actual_score = (actual_score * (100 - fifty)) / 100;
    return actual_score;
}
/*
special frc evals
*/
int SEARCHER::eval_frc_special() {
    int score = 0;

    /*corner trapped bishop*/
    if(board[A8] == bbishop && board[B7] == bpawn) {
        score += FRC_TRAPPED_BISHOP;
        if(board[B6] != blank)
             score += (FRC_TRAPPED_BISHOP >> 1);
    }
    if(board[H8] == bbishop && board[G7] == bpawn) {
        score += FRC_TRAPPED_BISHOP;
        if(board[G6] != blank)
             score += (FRC_TRAPPED_BISHOP >> 1);
    }
    if(board[A1] == wbishop && board[B2] == wpawn) {
        score -= FRC_TRAPPED_BISHOP;
        if(board[B3] != blank)
             score -= (FRC_TRAPPED_BISHOP >> 1);
    }
    if(board[H1] == wbishop && board[G2] == wpawn) {
        score -= FRC_TRAPPED_BISHOP;
        if(board[G3] != blank)
             score -= (FRC_TRAPPED_BISHOP >> 1);
    }

    /*score relative to player*/
    if(player == black) score = -score;
    return score;
}
/*
Hand-crafted evaluation
*/
const uint64_t lsquares = UINT64(0x55aa55aa55aa55aa);
const uint64_t dsquares = UINT64(0xaa55aa55aa55aa55);

static const uint8_t  mask[8] = {
    1,  2,  4,  8, 16, 32, 64,128
};
static const uint8_t  up_mask[8] = {
    254,252,248,240,224,192,128,  0
};
static const uint8_t  down_mask[8] = {
    0,  1,  3,  7, 15, 31, 63,127
};
static const uint8_t  updown_mask[8] = {
    254, 253, 251, 247, 239, 223, 191, 127
};

static int KING_ATTACK(int attack,int attackers,int tropism)
{
    if(attackers == 0) return 0;
    int score = ((9 * ATTACK_WEIGHT * (attack >> 4) + 1 * TROPISM_WEIGHT * tropism) >> 4);
    if(attackers == 1) return (score >> 2);
    int geometric = 2 << (attackers - 2);
    return ((score) * (geometric - 1)) / geometric;
}

int SEARCHER::eval_hce()
{
    int actual_score;

    /*phase of the game*/
    int phase = piece_c[white] + piece_c[black];
    phase = MIN(phase,MAX_MATERIAL);

    /*
    evaluate
    */
    SCORE w_score,b_score;
    int w_ksq = plist[wking]->sq;
    int b_ksq = plist[bking]->sq;
    int fw_ksq = file(w_ksq), rw_ksq = rank(w_ksq);
    int fb_ksq = file(b_ksq), rb_ksq = rank(b_ksq);
    int temp;

#ifdef TUNE
    material = phase;
#endif
    /*
    evaluate winning chances for some endgames.
    */
    eval_imbalance(w_score,b_score);

    /*trapped bishop*/
    if((board[A7] == wbishop || (board[B8] == wbishop && board[C7] == bpawn))
        && board[B6] == bpawn
        ) {
        w_score.sub(TRAPPED_BISHOP);
        if(board[C7] == bpawn)
            w_score.sub(TRAPPED_BISHOP >> 1);
    }
    if((board[H7] == wbishop || (board[G8] == wbishop && board[F7] == bpawn))
        && board[G6] == bpawn
        ) {
        w_score.sub(TRAPPED_BISHOP);
        if(board[F7] == bpawn) 
            w_score.sub(TRAPPED_BISHOP >> 1);
    }
    if((board[A2] == bbishop || (board[B1] == bbishop && board[C2] == wpawn))
        && board[B3] == wpawn
        ) {
        b_score.sub(TRAPPED_BISHOP);
        if(board[C2] == wpawn)
            b_score.sub(TRAPPED_BISHOP >> 1);
    }
    if((board[H2] == bbishop || (board[G1] == bbishop && board[F2] == wpawn))
        && board[G3] == wpawn
        ) {
        b_score.sub(TRAPPED_BISHOP);
        if(board[F2] == wpawn) 
            b_score.sub(TRAPPED_BISHOP >> 1);
    }

    /*trapped bishop at A6/H6/A3/H3*/
    if(board[A6] == wbishop && board[B5] == bpawn)
        w_score.sub((5 * TRAPPED_BISHOP) >> 3);
    if(board[H6] == wbishop && board[G5] == bpawn)
        w_score.sub((5 * TRAPPED_BISHOP) >> 3);
    if(board[A3] == bbishop && board[B4] == wpawn)
        b_score.sub((5 * TRAPPED_BISHOP) >> 3);
    if(board[H3] == bbishop && board[G4] == wpawn)
        b_score.sub((5 * TRAPPED_BISHOP) >> 3);

    /*trapped knight*/
    if(board[A7] == wknight && board[B7] == bpawn && (board[C6] == bpawn || board[A6] == bpawn))
        w_score.sub((5 * TRAPPED_BISHOP) >> 3);
    if(board[H7] == wknight && board[G7] == bpawn && (board[F6] == bpawn || board[H6] == bpawn)) 
        w_score.sub((5 * TRAPPED_BISHOP) >> 3);
    if(board[A2] == bknight && board[B2] == wpawn && (board[C3] == wpawn || board[A3] == wpawn)) 
        b_score.sub((5 * TRAPPED_BISHOP) >> 3);
    if(board[H2] == bknight && board[G2] == wpawn && (board[F3] == wpawn || board[H3] == wpawn)) 
        b_score.sub((5 * TRAPPED_BISHOP) >> 3);

    /*trapped knight at A8/H8/A1/H1*/
    if(board[A8] == wknight && (board[A7] == bpawn || board[C7] == bpawn))
        w_score.sub(TRAPPED_KNIGHT);
    if(board[H8] == wknight && (board[H7] == bpawn || board[G7] == bpawn))
        w_score.sub(TRAPPED_KNIGHT);
    if(board[A1] == bknight && (board[A2] == wpawn || board[C2] == wpawn))
        b_score.sub(TRAPPED_KNIGHT);
    if(board[H1] == bknight && (board[H2] == wpawn || board[G2] == wpawn))
        b_score.sub(TRAPPED_KNIGHT);

    /*trapped rook*/
    if((board[F1] == wking || board[G1] == wking) && 
        (board[H1] == wrook || board[H2] == wrook || board[G1] == wrook))
        w_score.subm(TRAPPED_ROOK);
    if((board[C1] == wking || board[B1] == wking) && 
        (board[A1] == wrook || board[A2] == wrook || board[B1] == wrook))
        w_score.subm(TRAPPED_ROOK);
    if((board[F8] == bking || board[G8] == bking) && 
        (board[H8] == brook || board[H7] == brook || board[G8] == brook))
        b_score.subm(TRAPPED_ROOK);
    if((board[C8] == bking || board[B8] == bking) && 
        (board[A8] == brook || board[A7] == brook || board[B8] == brook))
        b_score.subm(TRAPPED_ROOK);
    /*
    pawns
    */
    uint8_t all_pawn_f;
    uint64_t _wps = Rotate(pawns_bb[white]);
    uint64_t _bps = Rotate(pawns_bb[black]);
    uint8_t* wf_pawns = (uint8_t*) (&_wps);
    uint8_t* bf_pawns = (uint8_t*) (&_bps);
    int eval_w_attack = (man_c[wqueen] && piece_c[white] > 9);
    int eval_b_attack = (man_c[bqueen] && piece_c[black] > 9);

    w_score.add(eval_pawns(eval_w_attack,eval_b_attack,wf_pawns,bf_pawns));
    all_pawn_f = pawnrec.w_pawn_f | pawnrec.b_pawn_f;

    /*king attack/defence*/
    if(eval_b_attack) {
        b_score.addm(pawnrec.b_s_attack);
    }
    if(eval_w_attack) {
        w_score.addm(pawnrec.w_s_attack);
    }
    /*
    pieces
    */
    int w_tropism = 0;
    int b_tropism = 0;
    int w_on7th = 0;
    int b_on7th = 0;
    int wr_onhopen = 0;
    int br_onhopen = 0;
    int w_attack = 0;
    int b_attack = 0;
    int w_attackers = 0;
    int b_attackers = 0;
    int f,r,sq,c_sq,mob,opost;
    PLIST current;
    uint64_t bb;
    uint64_t noccupancyw = ~(pieces_bb[white] | pawns_bb[white]);
    uint64_t noccupancyb = ~(pieces_bb[black] | pawns_bb[black]);
    uint64_t occupancy = (~noccupancyw | ~noccupancyb);
    uint64_t wk_bb,bk_bb;
    uint64_t wkattacks_bb,bkattacks_bb;
    uint64_t wattacks_bb = UINT64(0),battacks_bb = UINT64(0);
    uint64_t wpattacks_bb = ((pawns_bb[white] & ~file_mask[FILEA]) << 7) |
                            ((pawns_bb[white] & ~file_mask[FILEH]) << 9);
    uint64_t bpattacks_bb = ((pawns_bb[black] & ~file_mask[FILEH]) >> 7) |
                            ((pawns_bb[black] & ~file_mask[FILEA]) >> 9);

    sq = w_ksq;
    if(fw_ksq == FILEA) sq++;
    else if(fw_ksq == FILEH) sq--;
    wkattacks_bb = king_attacks(SQ8864(sq));
    wk_bb = wkattacks_bb;
    wk_bb |= (wk_bb << 8);

    sq = b_ksq;
    if(fb_ksq == FILEA) sq++;
    else if(fb_ksq == FILEH) sq--;
    bkattacks_bb = king_attacks(SQ8864(sq));
    bk_bb = bkattacks_bb;
    bk_bb |= (bk_bb >> 8);

    /*exclude unsafe empty squares*/
    noccupancyw ^= (bpattacks_bb & ~occupancy);
    noccupancyb ^= (wpattacks_bb & ~occupancy);

    /*
    knights
    */
    current = plist[wknight];
    while(current) {
        c_sq = current->sq; 
        f = file(c_sq);
        r = rank(c_sq);

        /*mobility*/
        sq = SQ64(r,f);
        bb = knight_attacks(sq);
        wattacks_bb |= bb;

        mob = 3 * (popcnt(bb & noccupancyw) - 4);
        w_score.add(KNIGHT_MOB_MG * mob / 16, KNIGHT_MOB_EG * mob / 16);

        /*attack*/
        if(eval_w_attack) {
            w_tropism += piece_tropism[DISTANCE(f,r,fb_ksq,rb_ksq)];
            bb &= bk_bb;
            if(bb) {
                w_attack += KNIGHT_ATTACK * popcnt_sparse(bb);
                w_attackers++;
            }
        }

        /*knight outpost*/
        if(((r >= RANK4) && (r <= RANK7))
            && (f == FILEA || !(up_mask[r] & bf_pawns[f - 1]))
            && (f == FILEH || !(up_mask[r] & bf_pawns[f + 1]))
            ) {
                if(f >= FILEE) sq = SQ32(r,FILEH - f);
                else sq = SQ32(r,f);
                temp = outpost[sq];
                opost = temp;
                if(!(up_mask[r] & bf_pawns[f])) {
                    if(board[c_sq + LD] == wpawn) opost += temp;
                    if(board[c_sq + RD] == wpawn) opost += temp;
                } else {
                    if(board[c_sq + LD] == wpawn || board[c_sq + RD] == wpawn) 
                        opost += temp;
                }
                w_score.add(KNIGHT_OUTPOST_MG * opost / 16, KNIGHT_OUTPOST_EG * opost / 16);
        }
        /*pawn relation*/
        if(r >= RANK2 && r <= RANK4) {
            if(board[c_sq + UU] == wpawn)
                w_score.add(MINOR_BEHIND_PAWN);
        }
        /*end*/

        current = current->next;
    }

    current = plist[bknight];
    while(current) {
        c_sq = current->sq; 
        f = file(c_sq);
        r = rank(c_sq);

        /*mobility*/
        sq = SQ64(r,f);
        bb =  knight_attacks(sq);
        battacks_bb |= bb;

        mob = 3 * (popcnt(bb & noccupancyb) - 4);
        b_score.add(KNIGHT_MOB_MG * mob / 16, KNIGHT_MOB_EG * mob / 16);

        /*attack*/
        if(eval_b_attack) {
            b_tropism += piece_tropism[DISTANCE(f,r,fw_ksq,rw_ksq)];
            bb &= wk_bb;
            if(bb) {
                b_attack += KNIGHT_ATTACK * popcnt_sparse(bb);
                b_attackers++;
            }
        }
        /*knight outpost*/
        if(((r <= RANK5) && (r >= RANK2))
            && (f == FILEA || !(down_mask[r] & wf_pawns[f - 1]))
            && (f == FILEH || !(down_mask[r] & wf_pawns[f + 1]))
            ) {
                if(f >= FILEE) sq = SQ32(RANK8 - r,FILEH - f);
                else sq = SQ32(RANK8 - r,f);
                temp = outpost[sq];
                opost = temp;
                if(!(down_mask[r] & wf_pawns[f])) {
                    if(board[c_sq + LU] == bpawn) opost += temp;
                    if(board[c_sq + RU] == bpawn) opost += temp;
                } else {
                    if(board[c_sq + LU] == bpawn || board[c_sq + RU] == bpawn) 
                        opost += temp;
                }
                b_score.add(KNIGHT_OUTPOST_MG * opost / 16, KNIGHT_OUTPOST_EG * opost / 16);
        }
        /*pawn relation*/
        if(r >= RANK5 && r <= RANK7) {
            if(board[c_sq + DD] == bpawn)
                b_score.add(MINOR_BEHIND_PAWN);
        }
        /*end*/

        current = current->next;
    }
    /*
    bishops
    */
    current = plist[wbishop];
    while(current) {
        c_sq = current->sq;
        f = file(c_sq);
        r = rank(c_sq);

        /*mobility*/
        sq = SQ64(r,f);
        bb =  bishop_attacks(sq,occupancy);
        wattacks_bb |= bb;

        mob = 5 * (popcnt(bb & noccupancyw) - 6);
        w_score.add(BISHOP_MOB_MG * mob / 16, BISHOP_MOB_EG * mob / 16);

        /*attack*/
        if(eval_w_attack) {
            w_tropism += piece_tropism[DISTANCE(f,r,fb_ksq,rb_ksq)];
            bb &= bk_bb;
            if(bb) {
                w_attack += BISHOP_ATTACK * popcnt_sparse(bb);
                w_attackers++;
            }
        }

        /*bishop outpost*/
        if(((r >= RANK4) && (r <= RANK7))
            && (f == FILEA || !(up_mask[r] & bf_pawns[f - 1]))
            && (f == FILEH || !(up_mask[r] & bf_pawns[f + 1]))
            ) {
                if(f >= FILEE) sq = SQ32(r,FILEH - f);
                else sq = SQ32(r,f);
                temp = outpost[sq];
                opost = temp;
                if(!(up_mask[r] & bf_pawns[f])) {
                    if(board[c_sq + LD] == wpawn) opost += temp;
                    if(board[c_sq + RD] == wpawn) opost += temp;
                } else {
                    if(board[c_sq + LD] == wpawn || board[c_sq + RD] == wpawn) 
                        opost += temp;
                }
                w_score.add(BISHOP_OUTPOST_MG * opost / 16, BISHOP_OUTPOST_EG * opost / 16);
        }
        /*pawn relation*/
        if(r >= RANK2 && r <= RANK4) {
            if(board[c_sq + UU] == wpawn)
                w_score.add(MINOR_BEHIND_PAWN);
        }
        /*bad bishop*/
        if(is_light(c_sq))
            temp = popcnt_sparse(lsquares & pawns_bb[white]);
        else
            temp = popcnt_sparse(dsquares & pawns_bb[white]);

        w_score.sub(BAD_BISHOP_MG * temp, BAD_BISHOP_EG * temp);
        /*end*/

        current = current->next;
    }
    current = plist[bbishop];
    while(current) {
        c_sq = current->sq;
        f = file(c_sq);
        r = rank(c_sq);

        /*mobility*/
        sq = SQ64(r,f);
        bb =  bishop_attacks(sq,occupancy);
        battacks_bb |= bb;

        mob = 5 * (popcnt(bb & noccupancyb) - 6);
        b_score.add(BISHOP_MOB_MG * mob / 16, BISHOP_MOB_EG * mob / 16);

        /*attack*/
        if(eval_b_attack) {
            b_tropism += piece_tropism[DISTANCE(f,r,fw_ksq,rw_ksq)];
            bb &= wk_bb;
            if(bb) {
                b_attack += BISHOP_ATTACK * popcnt_sparse(bb);
                b_attackers++;
            }
        }

        /*bishop outpost*/
        if(((r <= RANK5) && (r >= RANK2))
            && (f == FILEA || !(down_mask[r] & wf_pawns[f - 1]))
            && (f == FILEH || !(down_mask[r] & wf_pawns[f + 1]))
            ) {
                if(f >= FILEE) sq = SQ32(RANK8 - r,FILEH - f);
                else sq = SQ32(RANK8 - r,f);
                temp = outpost[sq];
                opost = temp;
                if(!(down_mask[r] & wf_pawns[f])) {
                    if(board[c_sq + LU] == bpawn) opost += temp;
                    if(board[c_sq + RU] == bpawn) opost += temp;
                } else {
                    if(board[c_sq + LU] == bpawn || board[c_sq + RU] == bpawn) 
                        opost += temp;
                }
                b_score.add(BISHOP_OUTPOST_MG * opost / 16, BISHOP_OUTPOST_EG * opost / 16);
        }
        /*pawn relation*/
        if(r >= RANK5 && r <= RANK7) {
            if(board[c_sq + DD] == bpawn)
                b_score.add(MINOR_BEHIND_PAWN);
        }
        /*bad bishop*/
        if(is_light(c_sq))
            temp = popcnt_sparse(lsquares & pawns_bb[black]);
        else
            temp = popcnt_sparse(dsquares & pawns_bb[black]);

        b_score.sub(BAD_BISHOP_MG * temp, BAD_BISHOP_EG * temp);
        /*end*/

        current = current->next;
    }

    /*
    rooks
    */
    current = plist[wrook];
    while(current) {
        c_sq = current->sq;
        f = file(c_sq);
        r = rank(c_sq);

        /*mobility*/
        sq = SQ64(r,f);
        bb =  rook_attacks(sq,occupancy);
        wattacks_bb |= bb;

        mob = 3 * (popcnt(bb & noccupancyw) - 7);
        w_score.add(ROOK_MOB_MG * mob / 16, ROOK_MOB_EG * mob / 16);

        /*attack*/
        if(eval_w_attack) {
            w_tropism += piece_tropism[DISTANCE(f,r,fb_ksq,rb_ksq)];
            bb &= bk_bb;
            if(bb) {
                w_attack += (ROOK_ATTACK * popcnt_sparse(bb));
                w_attackers++;
            }
        }

        /*rook on 7th*/
        if(r == RANK7 && (rank(b_ksq) > r || (pawns_bb[black] & rank_mask[RANK7]))) {
            w_on7th += 2;
        } 

        /*sitting on open/half open file?*/
        if(!(mask[f] & all_pawn_f)) {
            wr_onhopen += 2;
            w_tropism += file_tropism[f_distance(c_sq, b_ksq)];
        } else if(!(mask[f] & pawnrec.w_pawn_f)) {
            wr_onhopen++;
            w_tropism += file_tropism[f_distance(c_sq, b_ksq)];
        } else if(!(mask[f] & pawnrec.b_pawn_f)) {
            w_tropism += file_tropism[f_distance(c_sq, b_ksq)] / 2;
        }

        /*support passed pawn*/
        if(pawnrec.w_passed & mask[f]) {
            if(r < last_bit[wf_pawns[f]])
                w_score.add(ROOK_SUPPORT_PASSED_MG,ROOK_SUPPORT_PASSED_EG);
        }
        if(pawnrec.b_passed & mask[f]) {
            if(r > first_bit[bf_pawns[f]])
                w_score.add(ROOK_SUPPORT_PASSED_MG,ROOK_SUPPORT_PASSED_EG);
        }
        /*end*/

        current = current->next;
    }
    current = plist[brook];
    while(current) {
        c_sq = current->sq;
        f = file(c_sq);
        r = rank(c_sq);

        /*mobility*/
        sq = SQ64(r,f);
        bb =  rook_attacks(sq,occupancy);
        battacks_bb |= bb;

        mob = 3 * (popcnt(bb & noccupancyb) - 7);
        b_score.add(ROOK_MOB_MG * mob / 16, ROOK_MOB_EG * mob / 16);

        /*attack*/
        if(eval_b_attack) {
            b_tropism += piece_tropism[DISTANCE(f,r,fw_ksq,rw_ksq)];
            bb &= wk_bb;
            if(bb) {
                b_attack += (ROOK_ATTACK * popcnt_sparse(bb));
                b_attackers++;
            }
        }

        /*rook on 7th*/
        if(r == RANK2 && (rank(w_ksq) < r || (pawns_bb[white] & rank_mask[RANK2]))) {
            b_on7th += 2;
        }

        /*sitting on open/half open file?*/
        if(!(mask[f] & all_pawn_f)) {
            br_onhopen += 2;
            b_tropism += file_tropism[f_distance(c_sq, w_ksq)];
        } else if(!(mask[f] & pawnrec.b_pawn_f)) {
            br_onhopen++;
            b_tropism += file_tropism[f_distance(c_sq, w_ksq)];
        } else if(!(mask[f] & pawnrec.w_pawn_f)) {
            b_tropism += file_tropism[f_distance(c_sq, w_ksq)] / 2;
        }

        /*support passed pawn*/
        if(pawnrec.b_passed & mask[f]) {
            if(r > first_bit[bf_pawns[f]])
                b_score.add(ROOK_SUPPORT_PASSED_MG,ROOK_SUPPORT_PASSED_EG);
        }
        if(pawnrec.w_passed & mask[f]) {
            if(r < last_bit[wf_pawns[f]])
                b_score.add(ROOK_SUPPORT_PASSED_MG,ROOK_SUPPORT_PASSED_EG);
        }
        /*end*/

        current = current->next;
    }

    /*
    queens
    */
    current = plist[wqueen];
    while(current) {
        c_sq = current->sq;
        f = file(c_sq);
        r = rank(c_sq);

        /*mobility*/
        sq = SQ64(r,f);
        bb =  queen_attacks(sq,occupancy);
        wattacks_bb |= bb;

        mob = (popcnt(bb & noccupancyw) - 13);
        w_score.add(QUEEN_MOB_MG * mob / 16, QUEEN_MOB_EG * mob / 16);

        /*attack*/
        if(eval_w_attack) {
            w_tropism += queen_tropism[DISTANCE(f,r,fb_ksq,rb_ksq)];
            bb &= bk_bb;
            if(bb) {
                w_attack += (QUEEN_ATTACK * popcnt_sparse(bb));
                w_attackers++;
            }
        }

        /*queen on 7th*/
        if(r == RANK7 && (rank(b_ksq) > r || (pawns_bb[black] & rank_mask[RANK7]))) {
            w_on7th += 3;
        }

        current = current->next;
    }
    current = plist[bqueen];
    while(current) {
        c_sq = current->sq;
        f = file(c_sq);
        r = rank(c_sq);

        /*mobility*/
        sq = SQ64(r,f);
        bb =  queen_attacks(sq,occupancy);
        battacks_bb |= bb;

        mob = (popcnt(bb & noccupancyb) - 13);
        b_score.add(QUEEN_MOB_MG * mob / 16, QUEEN_MOB_EG * mob / 16);

        /*attack*/
        if(eval_b_attack) {
            b_tropism += queen_tropism[DISTANCE(f,r,fw_ksq,rw_ksq)]; 
            bb &= wk_bb;
            if(bb) {
                b_attack += (QUEEN_ATTACK * popcnt_sparse(bb));
                b_attackers++;
            }
        }

        /*queen on 7th*/
        if(r == RANK2 && (rank(w_ksq) < r || (pawns_bb[white] & rank_mask[RANK2]))) {
            b_on7th += 3;
        }

        current = current->next;
    }

    /*score QR placement*/
    w_score.add(ROOK_ON_7TH * (qr_on_7thrank[w_on7th] - qr_on_7thrank[b_on7th]) / 16);
    w_score.add(ROOK_ON_OPEN * (rook_on_hopen[wr_onhopen] - rook_on_hopen[br_onhopen]) / 16);

    /*
    king eval
    */
    wattacks_bb |= wpattacks_bb;
    battacks_bb |= bpattacks_bb;

    if(eval_b_attack) {
        b_attack += pawnrec.b_attack;
        b_attack += UNDEFENDED_ATTACK * popcnt_sparse(battacks_bb & ~wattacks_bb & wk_bb);
        temp = KING_ATTACK(b_attack,b_attackers,b_tropism);
        b_score.addm(temp);
    }
    if(eval_w_attack) {
        w_attack += pawnrec.w_attack;
        w_attack += UNDEFENDED_ATTACK * popcnt_sparse(wattacks_bb & ~battacks_bb & bk_bb);
        temp = KING_ATTACK(w_attack,w_attackers,w_tropism);
        w_score.addm(temp);
    }

    /*
    hanging pieces
    */
    wattacks_bb |= wkattacks_bb;
    battacks_bb |= bkattacks_bb;

    bb = ((wattacks_bb & ~bpattacks_bb) | wpattacks_bb) & pieces_bb[black];
    if(bb) {
        temp = popcnt_sparse(bb);
        w_score.add(HANGING_PENALTY * temp * temp);
    }
    bb = ((battacks_bb & ~wpattacks_bb) | bpattacks_bb) & pieces_bb[white];
    if(bb) {
        temp = popcnt_sparse(bb);
        b_score.add(HANGING_PENALTY * temp * temp);
    }

    /*
    attacked pieces
    */
    bb = wattacks_bb & pieces_bb[black];
    if(bb) {
        temp = popcnt_sparse(bb);
        w_score.add(ATTACKED_PIECE * temp * temp);
    }
    bb = battacks_bb & pieces_bb[white];
    if(bb) {
        temp = popcnt_sparse(bb);
        b_score.add(ATTACKED_PIECE * temp * temp);
    }

    /*
    Defended pieces
    */
    bb = wattacks_bb & (pieces_bb[white] ^ BB(w_ksq));
    if(bb) {
        temp = popcnt_sparse(bb);
        w_score.add(DEFENDED_PIECE * temp);
    }
    bb = battacks_bb & (pieces_bb[black] ^ BB(b_ksq));
    if(bb) {
        temp = popcnt_sparse(bb);
        b_score.add(DEFENDED_PIECE * temp);
    }

    /*
    passed pawns
    */
    if(pawnrec.w_passed | pawnrec.b_passed) {
        temp = eval_passed_pawns(wf_pawns,bf_pawns,all_pawn_f,wattacks_bb,battacks_bb);
        w_score.add((PASSER_WEIGHT_MG * temp) / 16,(PASSER_WEIGHT_EG * temp) / 16); 
    }

    /*
    scaled evaluation by phase
    */
    if(player == white) {
        actual_score = ((w_score.mid - b_score.mid) * (phase) +
                        (w_score.end - b_score.end) * (MAX_MATERIAL - phase)) / MAX_MATERIAL;
    } else {
        actual_score = ((b_score.mid - w_score.mid) * (phase) +
                        (b_score.end - w_score.end) * (MAX_MATERIAL - phase)) / MAX_MATERIAL;
    }

    /*side to move*/
    actual_score += (TEMPO_BONUS + (phase * TEMPO_SLOPE) / MAX_MATERIAL);

    return actual_score;
}
/*
piece square tables - middle/endgame 
*/
void SEARCHER::pre_calculate() {
    int piece_value[] = {
        0,
        0,QUEEN_MG,ROOK_MG,BISHOP_MG,KNIGHT_MG,PAWN_MG,
        0,QUEEN_MG,ROOK_MG,BISHOP_MG,KNIGHT_MG,PAWN_MG,
        0,
        0,
        0,QUEEN_EG,ROOK_EG,BISHOP_EG,KNIGHT_EG,PAWN_EG,
        0,QUEEN_EG,ROOK_EG,BISHOP_EG,KNIGHT_EG,PAWN_EG,
        0
    };
    int pic,sq,sq1,sq1r;
    for(pic = wking; pic <= bpawn; pic++) {
        for(sq = 0; sq <= 128; sq++) {
            if(sq & 0x88) pcsq[pic][sq] = piece_value[pic + 14];
            else pcsq[pic][sq] = piece_value[pic];
        }
    }
    for(sq = 0; sq < 128; sq++) {
        if(file(sq) >= FILEE) sq1 = MIRRORF(sq);
        else sq1 = sq;
        if(sq1 & 0x88) sq1 -= 4;
        sq1 = SQ8864(sq1);
        sq1r = MIRRORR64(sq1);

        pcsq[wking][sq] += king_pcsq[sq1];
        pcsq[bking][sq] += king_pcsq[sq1r];
        pcsq[wknight][sq] += knight_pcsq[sq1];
        pcsq[bknight][sq] += knight_pcsq[sq1r];
        pcsq[wbishop][sq] += bishop_pcsq[sq1];
        pcsq[bbishop][sq] += bishop_pcsq[sq1r];
        pcsq[wrook][sq] += rook_pcsq[sq1];
        pcsq[brook][sq] += rook_pcsq[sq1r];
        pcsq[wqueen][sq] += queen_pcsq[sq1];
        pcsq[bqueen][sq] += queen_pcsq[sq1r];
        pcsq[wpawn][sq] += pawn_pcsq[sq1];
        pcsq[bpawn][sq] += pawn_pcsq[sq1r];
    }
}
/*
evaluate pawn cover
*/
void SEARCHER::eval_pawn_cover(int eval_w_attack,int eval_b_attack,
                         uint8_t* wf_pawns,uint8_t* bf_pawns
                         ) {
    
    int defence,hopen,f,r;
    int w_ksq = plist[wking]->sq;
    int b_ksq = plist[bking]->sq;
    uint8_t all_pawn_f = pawnrec.w_pawn_f | pawnrec.b_pawn_f;

    pawnrec.w_ksq = w_ksq;
    pawnrec.b_ksq = b_ksq;

    /*
    black attack
    */
    if(eval_b_attack) {
        
        pawnrec.b_ksq |= 0x80;
        pawnrec.b_s_attack = 0;
        
        defence = 0;
        hopen = 0;
        f = file(w_ksq);
        r = rank(w_ksq);
        
        uint64_t bb = (((pawns_bb[black] & ~file_mask[FILEH]) >> 7) |
                       ((pawns_bb[black] & ~file_mask[FILEA]) >> 9) ) &
                       king_attacks(SQ8864(w_ksq));
        pawnrec.b_attack = PAWN_ATTACK * popcnt_sparse(bb);


        /*pawn cover*/
        if(f < FILED) {
            if(board[C2] == wpawn) defence += 2;
            else if(board[C3] == wpawn) defence += 1;
            else if(board[C4] == wpawn) defence += 1;
            else hopen |= 1;
            
            if(board[B2] == wpawn) defence += 4;
            else if(board[B3] == wpawn) defence += 2;
            else if(board[B4] == wpawn) defence += 1;
            else hopen |= 2;
            
            if(board[A2] == wpawn) defence += 3;
            else if(board[A3] == wpawn) defence += 2;
            else if(board[A4] == wpawn) defence += 1;
            else hopen |= 4;

            if(board[B2] != wpawn) {
                defence--;
                if(board[C2] != wpawn) {
                    defence-=2;
                    if(board[C3] != wpawn)
                        defence--;
                }
            }
        } else if(f > FILEE) {
            if(board[F2] == wpawn) defence += 2;
            else if(board[F3] == wpawn) defence += 1;
            else if(board[F4] == wpawn) defence += 1;
            else hopen |= 1;
            
            if(board[G2] == wpawn) defence += 4;
            else if(board[G3] == wpawn) defence += 2;
            else if(board[G4] == wpawn) defence += 1;
            else hopen |= 2;
            
            if(board[H2] == wpawn) defence += 3;
            else if(board[H3] == wpawn) defence += 2;
            else if(board[H4] == wpawn) defence += 1;
            else hopen |= 4;

            if(board[G2] != wpawn) {
                defence--;
                if(board[F2] != wpawn) {
                    defence-=2;
                    if(board[F3] != wpawn)
                        defence--;
                }
            }
        }
        pawnrec.b_s_attack -= (PAWN_GUARD * 10 * defence) / 16; 
        
        /*pawn storm on white king*/
        if(f_distance(w_ksq,b_ksq) > 2)  {
            int r1,r2,r3;
            if(((r1 = first_bit[bf_pawns[f]]) == 8) || (r1 <= r - 1)) r1 = RANK8;
            if(((r2 = (f == FILEA ? 8 : first_bit[bf_pawns[f - 1]])) == 8) || (r2 <= r - 1)) r2 = RANK8;
            if(((r3 = (f == FILEH ? 8 : first_bit[bf_pawns[f + 1]])) == 8) || (r3 <= r - 1)) r3 = RANK8;
            r1 = RANK8 - r1; r2 = RANK8 - r2; r3 = RANK8 - r3;
            pawnrec.b_s_attack += ((r1*r1 + r2*r2 + r3*r3) * PAWN_STORM) / 16;
        }
        
        /*open files around king*/
        if(!(all_pawn_f & mask[f])) pawnrec.b_s_attack += KING_ON_OPEN;
        if(f > FILEA && !(all_pawn_f & mask[f - 1])) pawnrec.b_s_attack += KING_ON_OPEN / 2;
        if(f < FILEH && !(all_pawn_f & mask[f + 1])) pawnrec.b_s_attack += KING_ON_OPEN / 2;
        
        /*penalize king sitting on half open files,and centre squares*/
        pawnrec.b_attack += (king_on_file[f] +
                        king_on_rank[r] +
                        king_on_hopen[hopen]);
    }
    /*
    white attack
    */
    if(eval_w_attack) {

        pawnrec.w_ksq |= 0x80;
        pawnrec.w_s_attack = 0;
        defence = 0;
        hopen = 0;
        f = file(b_ksq);
        r = rank(b_ksq);
        
        uint64_t bb = (((pawns_bb[white] & ~file_mask[FILEA]) << 7) |
                       ((pawns_bb[white] & ~file_mask[FILEH]) << 9) ) &
                       king_attacks(SQ8864(b_ksq));
        pawnrec.w_attack = PAWN_ATTACK * popcnt_sparse(bb);
        
        /*pawn cover*/
        if(f < FILED) {
            if(board[C7] == bpawn) defence += 2;
            else if(board[C6] == bpawn) defence += 1;
            else if(board[C5] == bpawn) defence += 1;
            else hopen |= 1;
            
            if(board[B7] == bpawn) defence += 4;
            else if(board[B6] == bpawn) defence += 2;
            else if(board[B5] == bpawn) defence += 1;
            else hopen |= 2;
            
            if(board[A7] == bpawn) defence += 3;
            else if(board[A6] == bpawn) defence += 2;
            else if(board[A5] == bpawn) defence += 1;
            else hopen |= 4;

            if(board[B7] != bpawn) {
                defence--;
                if(board[C7] != bpawn) {
                    defence-=2;
                    if(board[C6] != bpawn)
                        defence--;
                }
            }
        } else if(f > FILEE) {
            if(board[F7] == bpawn) defence += 2;
            else if(board[F6] == bpawn) defence += 1;
            else if(board[F5] == bpawn) defence += 1;
            else hopen |= 1;
            
            if(board[G7] == bpawn) defence += 4;
            else if(board[G6] == bpawn) defence += 2;
            else if(board[G5] == bpawn) defence += 1;
            else hopen |= 2;
            
            if(board[H7] == bpawn) defence += 3;
            else if(board[H6] == bpawn) defence += 2;
            else if(board[H5] == bpawn) defence += 1;
            else hopen |= 4;

            if(board[G7] != bpawn) {
                defence--;
                if(board[F7] != bpawn) {
                    defence-=2;
                    if(board[F6] != bpawn)
                        defence--;
                }
            }
        }
        pawnrec.w_s_attack -= (PAWN_GUARD * 10 * defence) / 16;
        
        /*pawn storm on black king*/
        if(f_distance(w_ksq,b_ksq) > 2)  {
            int r1,r2,r3;
            if(((r1 = last_bit[wf_pawns[f]]) == 8) || (r1 >= r + 1)) r1 = RANK1;
            if(((r2 = (f == FILEA ? 8 : last_bit[wf_pawns[f - 1]])) == 8) || (r2 >= r + 1)) r2 = RANK1;
            if(((r3 = (f == FILEH ? 8 : last_bit[wf_pawns[f + 1]])) == 8) || (r3 >= r + 1)) r3 = RANK1;
            pawnrec.w_s_attack += ((r1*r1 + r2*r2 + r3*r3) * PAWN_STORM) / 16;
        }
        
        /*open files around king*/
        if(!(all_pawn_f & mask[f])) pawnrec.w_s_attack += KING_ON_OPEN;
        if(f > FILEA && !(all_pawn_f & mask[f - 1])) pawnrec.w_s_attack += KING_ON_OPEN / 2;
        if(f < FILEH && !(all_pawn_f & mask[f + 1])) pawnrec.w_s_attack += KING_ON_OPEN / 2;
        
        /*penalize king sitting on half open files and centre squares*/
        pawnrec.w_attack += (king_on_file[f] +
            king_on_rank[RANK8 - r] +
            king_on_hopen[hopen]);
    }
}
/*
pawn evaluation
*/
#define wp_attacks(sq) ((board[sq + LD] == wpawn) + (board[sq + RD] == wpawn))
#define bp_attacks(sq) ((board[sq + LU] == bpawn) + (board[sq + RU] == bpawn))

SCORE SEARCHER::eval_pawns(int eval_w_attack,int eval_b_attack,
                         uint8_t* wf_pawns,uint8_t* bf_pawns) {
    SCORE score;
    PLIST pawnl;

    pawnrec.w_ksq = 0;
    pawnrec.b_ksq = 0;

#ifndef TUNE
    if(probe_pawn_hash(pawn_hash_key,score,pawnrec)) {
        /*evaluate pawn cover*/
        if(eval_w_attack || eval_b_attack) {
            if(    (pawnrec.w_ksq & 0x7f) == plist[wking]->sq 
                && (pawnrec.b_ksq & 0x7f) == plist[bking]->sq
                && (pawnrec.w_ksq >> 7) >= eval_w_attack
                && (pawnrec.b_ksq >> 7) >= eval_b_attack );
            else  {
                eval_pawn_cover(eval_w_attack,eval_b_attack,wf_pawns,bf_pawns);
            }
        }
    } else {
#endif
        int sq,tsq,f,r;
        /*zero*/
        score.zero();
        pawnrec.w_passed = 0;
        pawnrec.b_passed = 0;
        pawnrec.w_pawn_f = 0;
        pawnrec.b_pawn_f = 0;
        
        /*white*/
        pawnl = plist[wpawn];
        while(pawnl) {
            sq = pawnl->sq;
            f = file(sq);
            r = rank(sq);
            
            pawnrec.w_pawn_f |= mask[f];
            
            /*doubled pawns*/
            if(updown_mask[r] & wf_pawns[f]) {
                score.sub(PAWN_DOUBLED_MG,PAWN_DOUBLED_EG);
            }
            /*duo/supported/weak/isolated pawns*/
            if(wp_attacks(sq + UU)) {
                score.add(PAWN_DUO_MG, PAWN_DUO_EG);
            } else if(wp_attacks(sq)) {
                score.add(PAWN_SUPPORTED_MG,PAWN_SUPPORTED_EG);
            } else if((f == FILEA || !wf_pawns[f - 1]) &&
                      (f == FILEH || !wf_pawns[f + 1]) ) {
                if(!(up_mask[r] & (bf_pawns[f] | wf_pawns[f]))) 
                    score.sub(PAWN_ISOLATED_ON_OPEN_MG,PAWN_ISOLATED_ON_OPEN_EG);
                else 
                    score.sub(PAWN_ISOLATED_ON_CLOSED_MG,PAWN_ISOLATED_ON_CLOSED_EG);
            } else if(wp_attacks(sq) <= bp_attacks(sq)) { 
                
                bool is_weak = true;
                /*can it be supported by pushing a pawn from left?*/
                if(r >= RANK4
                    && board[sq + LDD] == wpawn
                    && PIECE(board[sq + LD]) != pawn
                    && wp_attacks(sq + LD) >= bp_attacks(sq + LD)) 
                    is_weak = false;
                /*...from right?*/
                else if(r >= RANK4
                    && board[sq + RDD] == wpawn
                    && PIECE(board[sq + RD]) != pawn
                    && wp_attacks(sq + RD) >= bp_attacks(sq + RD)) 
                    is_weak = false;
                /*can it advace and be supported?*/
                else if(PIECE(board[sq + UU]) != pawn
                    && (wp_attacks(sq + UU) > bp_attacks(sq + UU) ||
                    (wp_attacks(sq + UU) == bp_attacks(sq + UU) && wp_attacks(sq) < bp_attacks(sq))
                    ))
                    is_weak = false;
                
                /*on open file?*/
                if(is_weak) {
                    if(!(up_mask[r] & (bf_pawns[f] | wf_pawns[f])))
                        score.sub(PAWN_WEAK_ON_OPEN_MG,PAWN_WEAK_ON_OPEN_EG);
                    else
                        score.sub(PAWN_WEAK_ON_CLOSED_MG,PAWN_WEAK_ON_CLOSED_EG);
                }
                /*end*/
            }
            
            /*passed/candidate pawns*/
            if((f == FILEA || !(up_mask[r] & bf_pawns[f - 1])) &&
                !(up_mask[r] & bf_pawns[f]) &&
                (f == FILEH || !(up_mask[r] & bf_pawns[f + 1])) ) {
                pawnrec.w_passed |= mask[f]; 
            } else if(!(up_mask[r] & (wf_pawns[f] | bf_pawns[f]))) {
                
                bool is_candidate = true;
                for(tsq = sq + UU;tsq < A8 + f;tsq += UU) {
                    if(wp_attacks(tsq) < bp_attacks(tsq)) {
                        is_candidate = false;
                        break;
                    }
                }
                if(is_candidate)
                    score.add(CANDIDATE_PP_MG * passed_rank_bonus[r] / 32,
                              CANDIDATE_PP_EG * passed_rank_bonus[r] / 32);
            }
            /*end*/
            pawnl = pawnl->next;
        }
        
        /*black*/
        pawnl = plist[bpawn];
        while(pawnl) {
            sq = pawnl->sq;
            f = file(sq);
            r = rank(sq);
            
            pawnrec.b_pawn_f |= mask[f];
            
            /*doubled pawns*/
            if(updown_mask[r] & bf_pawns[f]) {
                score.add(PAWN_DOUBLED_MG,PAWN_DOUBLED_EG);
            }
            /*duo/supported/weak/isolated pawns*/
            if(bp_attacks(sq + DD)) {
                score.sub(PAWN_DUO_MG, PAWN_DUO_EG);
            } else if(bp_attacks(sq)) {
                score.sub(PAWN_SUPPORTED_MG,PAWN_SUPPORTED_EG);
            } else if((f == FILEA || !bf_pawns[f - 1]) &&
                      (f == FILEH || !bf_pawns[f + 1]) ) {
                if(!(down_mask[r] & (bf_pawns[f] | wf_pawns[f])))
                    score.add(PAWN_ISOLATED_ON_OPEN_MG,PAWN_ISOLATED_ON_OPEN_EG);
                else
                    score.add(PAWN_ISOLATED_ON_CLOSED_MG,PAWN_ISOLATED_ON_CLOSED_EG);
            } else if(bp_attacks(sq) <= wp_attacks(sq)) {
                
                bool is_weak = true;
                /*can it be supported by pushing a pawn from left?*/
                if(r <= RANK5
                    && board[sq + LUU] == bpawn
                    && PIECE(board[sq + LU]) != pawn
                    && bp_attacks(sq + LU) >= wp_attacks(sq + LU))  
                    is_weak = false;
                /*...from right?*/
                else if(r <= RANK5
                    && board[sq + RUU] == bpawn
                    && PIECE(board[sq + RU]) != pawn
                    && bp_attacks(sq + RU) >= wp_attacks(sq + RU))  
                    is_weak = false;
                /*can it advace and be supported?*/
                else if(PIECE(board[sq + DD]) != pawn
                    && (bp_attacks(sq + DD) > wp_attacks(sq + DD) ||
                    (bp_attacks(sq + DD) == wp_attacks(sq + DD) && bp_attacks(sq) < wp_attacks(sq))
                    ))
                    is_weak = false;
                
                /*on open file?*/
                if(is_weak) {
                    if(!(down_mask[r] & (bf_pawns[f] | wf_pawns[f])))
                        score.add(PAWN_WEAK_ON_OPEN_MG,PAWN_WEAK_ON_OPEN_EG);
                    else
                        score.add(PAWN_WEAK_ON_CLOSED_MG,PAWN_WEAK_ON_CLOSED_EG);
                }
                /*end*/
            }
            
            /*passed/candidate pawns*/
            if((f == FILEA || !(down_mask[r] & wf_pawns[f - 1])) &&
                !(down_mask[r] & wf_pawns[f]) &&
                (f == FILEH || !(down_mask[r] & wf_pawns[f + 1])) ) {
                pawnrec.b_passed |= mask[f];
            } else if(!(down_mask[r] & (wf_pawns[f] | bf_pawns[f]))) {
                
                bool is_candidate = true;
                for(tsq = sq + DD;tsq > A1 + f;tsq += DD) {
                    if(bp_attacks(tsq) < wp_attacks(tsq)) {
                        is_candidate = false;
                        break;
                    }
                }
                if(is_candidate) 
                    score.sub(CANDIDATE_PP_MG * passed_rank_bonus[7 - r] / 32,
                              CANDIDATE_PP_EG * passed_rank_bonus[7 - r] / 32);
            }
            /*end*/
            pawnl = pawnl->next;
        }
        /*evaluate pawn cover*/
        if(eval_w_attack || eval_b_attack) {
            eval_pawn_cover(eval_w_attack,eval_b_attack,wf_pawns,bf_pawns);
        }

#ifndef TUNE
        /*store in hash table*/

        record_pawn_hash(pawn_hash_key,score,pawnrec);
    }
#endif
    return score;
}

/*
passed pawn evaluation
*/
static uint64_t northFill(uint64_t b) {
   b |= (b <<  8);
   b |= (b << 16);
   b |= (b << 32);
   return b;
}
static uint64_t southFill(uint64_t b) {
   b |= (b >>  8);
   b |= (b >> 16);
   b |= (b >> 32);
   return b;
}
int SEARCHER::eval_passed_pawns(uint8_t* wf_pawns,uint8_t* bf_pawns,uint8_t& all_pawn_f,
                const uint64_t& wattacks_bb, const uint64_t& battacks_bb) {
    uint8_t passed;
    int sq,f,r;
    int w_score,b_score,passed_score,rank_score, temp;
    int qdist,w_best_qdist = RANK8,b_best_qdist = RANK8;
    int w_ksq = plist[wking]->sq;
    int b_ksq = plist[bking]->sq;
    uint64_t passer_bb;

    w_score = 0;
    b_score = 0;
    
    passer_bb = UINT64(0);
    passed = pawnrec.w_passed;
    while(passed) {
        f = first_bit[passed];
        r = last_bit[wf_pawns[f]];
        sq = SQ(r,f);

        passed_score = rank_score = 
            passed_rank_bonus[r] + passed_file_bonus[(f >= FILEE) ? (FILEH - f) : f];

        //blocked
        if(board[sq + UU] != blank)
            passed_score -= (PASSER_BLOCKED * rank_score) / 32;
        else
            passer_bb |= BB(sq);

        //distance to kings
        passed_score += (PASSER_KING_ATTACK * rank_score * distance(b_ksq,sq + UU)) / 128;
        passed_score += (PASSER_KING_SUPPORT * rank_score * (9 - distance(sq,w_ksq))) / 256;
        
        //opponent has no pieces
        if(piece_c[black] == 0) {
            qdist = RANK8 - r;
            if(player == black) qdist++;
            for(int tsq = sq + UU;tsq <= A8 + f;tsq += UU) {
                if(board[tsq] != blank) qdist++;
            }
            if(r == RANK2 && board[sq + UU] == blank && board[sq + UUU] == blank) 
                qdist--;
            if(qdist < distance(b_ksq,A8 + f)) {
                w_best_qdist = MIN(qdist,w_best_qdist);
            }
        }
        
        w_score += passed_score;

        passed ^= mask[f];
    }
    if(passer_bb) {
        passer_bb = northFill(passer_bb);
        temp = popcnt_sparse(passer_bb & (battacks_bb | pieces_bb[black]));
        if(!temp) temp = -4; //all free passers
        w_score -= (PASSER_ATTACK * temp) / 16;
        temp = popcnt_sparse(passer_bb & (wattacks_bb & battacks_bb));
        w_score += (PASSER_SUPPORT * temp) / 16;
    }

    passer_bb = UINT64(0);
    passed = pawnrec.b_passed;
    while(passed) {
        f = first_bit[passed];
        r = first_bit[bf_pawns[f]];
        sq = SQ(r,f);

        passed_score = rank_score = 
            passed_rank_bonus[7 - r] + passed_file_bonus[(f >= FILEE) ? (FILEH - f) : f];
        
        //blocked
        if(board[sq + DD] != blank)
            passed_score -= (PASSER_BLOCKED * rank_score) / 32;
        else
            passer_bb |= BB(sq);

        //distance to kings
        passed_score += (PASSER_KING_ATTACK * rank_score * distance(w_ksq,sq + DD)) / 128;
        passed_score += (PASSER_KING_SUPPORT * rank_score * (9 - distance(b_ksq,sq))) / 256;
            
        //opponent has no pieces
        if(piece_c[white] == 0) {
            qdist = r;
            if(player == white) qdist++;
            for(int tsq = sq + DD;tsq >= A1 + f;tsq += DD) {
                if(board[tsq] != blank) qdist++;
            }
            if(r == RANK7 && board[sq + DD] == blank && board[sq + DDD] == blank) 
                qdist--;
            if(qdist < distance(w_ksq,A1 + f)) {
                b_best_qdist = MIN(qdist,b_best_qdist);
            }
        }

        b_score += passed_score;

        passed ^= mask[f];
    }
    if(passer_bb) {
        passer_bb = southFill(passer_bb);
        temp = popcnt_sparse(passer_bb & (wattacks_bb | pieces_bb[white]));
        if(!temp) temp = -4; //all free passers
        b_score -= (PASSER_ATTACK * temp) / 16;
        temp = popcnt_sparse(passer_bb & (wattacks_bb & battacks_bb));
        b_score += (PASSER_SUPPORT * temp) / 16;
    }

    /*unstoppable passer*/
    if(w_best_qdist < b_best_qdist) {
        w_score += 600;
    } else if(b_best_qdist < w_best_qdist) {
        b_score += 600;
    }
    
    /*king close to pawn center*/
    if(!piece_c[white] || !piece_c[black]) {
        int wclos = (7 - ABS(center_bit[all_pawn_f] - file(w_ksq)));
        int bclos = (7 - ABS(center_bit[all_pawn_f] - file(b_ksq)));
        if(wclos > bclos) 
            w_score += king_to_pawns[wclos - bclos];
        else if(bclos > wclos)
            b_score += king_to_pawns[bclos - wclos];
    }
    /*defender is knight*/
    int strech,pawns;
    if((pawns = pawnrec.w_pawn_f) != 0 && piece_c[black] == 3 && man_c[bknight] == 1) {
        strech = last_bit[pawns] - first_bit[pawns];
        if(pawnrec.w_passed) {
            if(strech > 2) w_score += PAWNS_VS_KNIGHT;
            else w_score += PAWNS_VS_KNIGHT / 2;
        } else {
            if(strech > 2) w_score += PAWNS_VS_KNIGHT / 2;
            else w_score += PAWNS_VS_KNIGHT / 4;
        }
    }
    if((pawns = pawnrec.b_pawn_f) != 0 && piece_c[white] == 3 && man_c[wknight] == 1) {
        strech = last_bit[pawns] - first_bit[pawns];
        if(pawnrec.b_passed) {
            if(strech > 2) b_score += PAWNS_VS_KNIGHT;
            else b_score += PAWNS_VS_KNIGHT / 2;
        } else {
            if(strech > 2) b_score += PAWNS_VS_KNIGHT / 2;
            else b_score += PAWNS_VS_KNIGHT / 4;
        }
    }
    /*end*/
    return (w_score - b_score);
}
/*
material score
*/
void SEARCHER::eval_imbalance(SCORE& w_score,SCORE& b_score) {
    int w_ksq = plist[wking]->sq;
    int b_ksq = plist[bking]->sq;
    int w_piece_value_c = piece_c[white];
    int b_piece_value_c = piece_c[black];
    int w_pawn_c = man_c[wpawn];
    int b_pawn_c = man_c[bpawn];
    int w_knight_c = man_c[wknight];
    int b_knight_c = man_c[bknight];
    int w_bishop_c = man_c[wbishop];
    int b_bishop_c = man_c[bbishop];
    int w_minors = w_knight_c + w_bishop_c;
    int b_minors = b_knight_c + b_bishop_c;
    int temp,temp1,temp2;

    /*
    Material and pcsq tables
    */
    w_score = pcsq_score[white];
    b_score = pcsq_score[black];

    /*
    imbalance
    */
    temp = w_piece_value_c - b_piece_value_c;
    temp1 = w_minors - b_minors;
    temp2 = w_pawn_c - b_pawn_c;

    if(temp >= 5) {                                     //[R,Q] vs Ps
        if(temp2 <= -(temp-1)) 
            w_score.add(MAJOR_v_P, MAJOR_v_P / 2);       
    } else if(temp <= -5 ) {
        if(temp2 >= -(temp+1))
            b_score.add(MAJOR_v_P, MAJOR_v_P / 2);
    } else if(temp >= 3) {                              //M vs 3P , Q vs R+4P
        if(temp2 <= -temp) 
            w_score.add(MINOR_v_P, MINOR_v_P / 2);         
    } else if(temp <= -3) {
        if(temp2 >= -temp)
            b_score.add(MINOR_v_P, MINOR_v_P / 2);
    } else if(temp1 >= 3) {                             //3M vs (2R or Q)
        if(ABS(temp) <= 1)
            w_score.add(MINORS3_v_MAJOR);                 
    } else if(temp1 <= -3) {
        if(ABS(temp) <= 1) 
            b_score.add(MINORS3_v_MAJOR);
    } else if(temp1 == 2) {                             //2M vs R
        if(ABS(temp) <= 1) 
            w_score.add(MINORS2_v_MAJOR);               
    } else if(temp1 == -2) {
        if(ABS(temp) <= 1) 
            b_score.add(MINORS2_v_MAJOR);
    } else if(temp == 2) {                              //R vs M
        if(temp2 <= -1)
            w_score.add(ROOK_v_MINOR);                  
    } else if(temp == -2) {
        if(temp2 >= 1)
            b_score.add(ROOK_v_MINOR);
    }
    /*
    bishop pair
    */
    if(w_bishop_c >= 2) {
        if(!b_minors)
            temp = 80;
        else if(!b_bishop_c)
            temp = 70;
        else
            temp = 50;

        w_score.add((BISHOP_PAIR_MG * temp) / 16,(BISHOP_PAIR_EG * temp) / 16);
    }
    if(b_bishop_c >= 2) {
        if(!w_minors)
            temp = 80;
        else if(!w_bishop_c)
            temp = 70;
        else
            temp = 50;

        b_score.add((BISHOP_PAIR_MG * temp) / 16,(BISHOP_PAIR_EG * temp) / 16);
    }

    /*
    pawn endgames
    */
    if(w_piece_value_c + b_piece_value_c == 0) {
        if(w_pawn_c > b_pawn_c) w_score.adde(EXTRA_PAWN);
        else if(b_pawn_c > w_pawn_c) b_score.adde(EXTRA_PAWN);
    }
    /*
    mating lone king
    */
    if(!b_piece_value_c && !b_pawn_c && w_piece_value_c >= 5) {
        w_score.adde(500 - 10 * distance(w_ksq,b_ksq)); 
    }
    if(!w_piece_value_c && !w_pawn_c && b_piece_value_c >= 5) {
        b_score.adde(500 - 10 * distance(w_ksq,b_ksq)); 
    }
}
/*
winning chances
*/
void SEARCHER::eval_win_chance(int& w_win_chance,int& b_win_chance) {
    int w_piece_value_c = piece_c[white];
    int b_piece_value_c = piece_c[black];
    int w_pawn_c = man_c[wpawn];
    int b_pawn_c = man_c[bpawn];
    int w_knight_c = man_c[wknight];
    int b_knight_c = man_c[bknight];
    int w_bishop_c = man_c[wbishop];
    int b_bishop_c = man_c[bbishop];
    /*
    KBK,KNK,KNNK
    */
    if(w_pawn_c == 0) {
        if(w_piece_value_c <= 3 || (w_piece_value_c == 6 && w_knight_c == 2)) {
            w_win_chance = 0;
        }
    }
    if(b_pawn_c == 0) {
        if(b_piece_value_c <= 3 || (b_piece_value_c == 6 && b_knight_c == 2)) {
            b_win_chance = 0;
        }
    }
    
    /*
    KBP*K draw also K(rook's P)*K draw
    */
    if(w_piece_value_c == 0 || (w_piece_value_c == 3 && w_bishop_c == 1)) { 
        int psq = A1,pfile = 0,bsq = (w_piece_value_c == 0) ? 
                        A1 : plist[wbishop]->sq,ksq = plist[bking]->sq;
        PLIST current = plist[wpawn];
        while(current) {
            if(current->sq > psq)
                psq = current->sq;
            pfile |= mask[file(current->sq)];
            if(pfile != mask[FILEA] && pfile != mask[FILEH])
                break;
            current = current->next;
        }
        
        if(pfile == mask[FILEA]
            && (w_piece_value_c == 0 || !is_light(bsq))) {
            if(distance(ksq,A8) <= 1 ||
                (rank(psq) < rank(ksq) && file(ksq) == FILEB)) {
                bool is_draw = true;
                for(int sq = psq + RR;sq < B8;sq += UU) {
                    if(board[sq] == bpawn) {
                        is_draw = false;
                        break;
                    }
                }
                if(is_draw) w_win_chance = 0;
            }
        } else if(pfile == mask[FILEH] 
            && (w_piece_value_c == 0 || is_light(bsq))) {
            if(distance(ksq,H8) <= 1 ||
                (rank(psq) < rank(ksq) && file(ksq) == FILEG)) {
                bool is_draw = true;
                for(int sq = psq + LL;sq < G8;sq += UU) {
                    if(board[sq] == bpawn) {
                        is_draw = false;
                        break;
                    }
                }
                if(is_draw) w_win_chance = 0;
            }
        }
    }
    
    if(b_piece_value_c == 0 || (b_piece_value_c == 3 && b_bishop_c == 1)) { 
        int psq = H8,pfile = 0,bsq = (b_piece_value_c == 0) ? 
                        A1 : plist[bbishop]->sq,ksq = plist[wking]->sq;
        PLIST current = plist[bpawn];
        while(current) {
            if(current->sq < psq)
                psq = current->sq;
            pfile |= mask[file(current->sq)];
            if(pfile != mask[FILEA] && pfile != mask[FILEH])
                break;
            current = current->next;
        }
        
        if(pfile == mask[FILEA] 
            && (b_piece_value_c == 0 || is_light(bsq))) {
            if(distance(ksq,A1) <= 1 ||
                (rank(psq) > rank(ksq) && file(ksq) == FILEB)) {
                bool is_draw = true;
                for(int sq = psq + RR;sq > B1;sq += DD) {
                    if(board[sq] == wpawn) {
                        is_draw = false;
                        break;
                    }
                }
                if(is_draw) b_win_chance = 0;
            }
        } else if(pfile == mask[FILEH] 
            && (b_piece_value_c == 0 || !is_light(bsq))) {
            if(distance(ksq,H1) <= 1 ||
                (rank(psq) > rank(ksq) && file(ksq) == FILEG)) {
                bool is_draw = true;
                for(int sq = psq + LL;sq > G1;sq += DD) {
                    if(board[sq] == wpawn) {
                        is_draw = false;
                        break;
                    }
                }
                if(is_draw) b_win_chance = 0;
            }
        }
    }

    /*
    no pawns
    */
    if(!w_pawn_c
        && w_piece_value_c >= b_piece_value_c 
        && w_piece_value_c - b_piece_value_c <= 3
        ) {
        if(w_bishop_c == 2 && b_knight_c == 1)
            w_win_chance /= 2;
        else
            w_win_chance /= 8;
    }
    if(!b_pawn_c
        && b_piece_value_c >= w_piece_value_c
        && b_piece_value_c - w_piece_value_c <= 3
        ) {
        if(b_bishop_c == 2 && w_knight_c == 1)
            b_win_chance /= 2;
        else
            b_win_chance /= 8;
    }
    /*
    one pawn
    */
    if(w_pawn_c == 1
        && w_piece_value_c
        && w_piece_value_c <= b_piece_value_c
        ) {
        if(w_piece_value_c == 3) {
            w_win_chance /= 8;
        } else if(w_piece_value_c == 6 && w_knight_c == 2) {
            w_win_chance /= 8;
        } else if(b_piece_value_c - w_piece_value_c >= 3) {
            w_win_chance /= 8;
        } else {
            w_win_chance /= 4;
        }
    }
    if(b_pawn_c == 1
        && b_piece_value_c
        && b_piece_value_c <= w_piece_value_c
        ) {
        if(b_piece_value_c == 3) {
            b_win_chance /= 8;
        } else if(b_piece_value_c == 6 && b_knight_c == 2) {
            b_win_chance /= 8;
        } else if(w_piece_value_c - b_piece_value_c >= 3) {
            b_win_chance /= 8;
        } else {
            b_win_chance /= 4;
        }
    }
    /*
    opposite colored bishop endings
    */
    if(w_piece_value_c + b_piece_value_c <= 16
        && w_piece_value_c == b_piece_value_c
        && w_bishop_c == 1 
        && b_bishop_c == 1 
        && is_light(plist[wbishop]->sq) != is_light(plist[bbishop]->sq)
        && ABS(w_pawn_c - b_pawn_c) <= 2
        ) {
        if(w_piece_value_c + b_piece_value_c == 6) {
            if(w_pawn_c >= b_pawn_c)
                w_win_chance /= 2;
            if(b_pawn_c >= w_pawn_c)
                b_win_chance /= 2;
        } else {
            if(w_pawn_c >= b_pawn_c)
                w_win_chance = (3 * w_win_chance) / 4;
            if(b_pawn_c >= w_pawn_c)
                b_win_chance = (3 * b_win_chance) / 4; 
        }
    }
}
/*
* Modify eval parameters
*/
#ifdef TUNE
/*
elo curve
*/
static PARAM ELO_MODEL = 0;
static int LR_FACTOR = 100;
static const int nPadJac = 3;

static inline double elo_to_gamma(double eloDelta) {
    return pow(10.0,eloDelta / 400.0);
}
static inline double nlogistic(double eloDelta) {
    return 1 / (1 + pow(10.0,eloDelta / 400.0));
}
static inline double ngaussian(double eloDelta) {
    return (1 + erf(-eloDelta / 400.0)) / 2;
}
static double win_prob(double eloDelta, int eloH, int eloD) {
    if(ELO_MODEL == 0) {
        return nlogistic(-eloDelta - eloH + eloD);
    } else if(ELO_MODEL == 1) {
        double thetaD = elo_to_gamma(eloD);
        double f = thetaD * sqrt(nlogistic(eloDelta + eloH) * nlogistic(-eloDelta - eloH));
        return nlogistic(-eloDelta - eloH) / (1 + f);
    } else {
        return ngaussian(-eloDelta - eloH + eloD);
    }
}
static double loss_prob(double eloDelta, int eloH, int eloD) {
    if(ELO_MODEL == 0) {
        return nlogistic(eloDelta + eloH + eloD);
    } else if(ELO_MODEL == 1) {
        double thetaD = elo_to_gamma(eloD);
        double f = thetaD * sqrt(nlogistic(eloDelta + eloH) * nlogistic(-eloDelta - eloH));
        return nlogistic(eloDelta + eloH) / (1 + f);
    } else {
        return ngaussian(eloDelta + eloH + eloD);
    }
}
static double get_scale(double eloD, double eloH) {
    const double K = log(10)/400.0;
    double df = 0;
    if(ELO_MODEL == 0) {
        double f = 1.0 / (1 + exp(-K*(eloD - eloH)));
        df = f * (1 - f) * K;
    } else if(ELO_MODEL == 1) {
        double dg = elo_to_gamma(eloD) - 1;
        double f = 1.0 / (1 + exp(-K*(eloD - eloH)));
        double dfx = f * (1 - f);
        double dx = dg * sqrt(dfx);
        double b = 1 + dx;
        double c = (dg * f * (1 - 2 * f)) / (2 * sqrt(dfx));
        df = ((b - c) / (b * b)) * dfx * K;
    } else if(ELO_MODEL == 2) {
        const double pi = 3.14159265359;
        double x = -(eloD - eloH)/400.0;
        df = exp(-x*x) / (400.0 * sqrt(pi));
    }
    return (4.0 / K) * df;
}
static FORCEINLINE double nlogp(double p) {
    return (p > 1e-12) ? -log(p) : -log(1e-12);
}
double get_log_likelihood(double result, double se) {
    static const double epsilon = 1e-4;
    static const double eloH = 0;  //we have STM bonus
    double factor_m = double(material) / MAX_MATERIAL;
    double eloD = ELO_DRAW + factor_m * ELO_DRAW_SLOPE_PHASE;
    double scale = get_scale(eloD,eloH);
    se = se / scale;

    if(result >= 1 - epsilon)
        return nlogp(win_prob(se,eloH,eloD));
    else if(result <= epsilon)
        return nlogp(loss_prob(se,eloH,eloD));
    else if(fabs(result - 0.5) <= epsilon)
        return nlogp(1 - win_prob(se,eloH,eloD) - loss_prob(se,eloH,eloD));
    else {
        double drawp = 0.7 * MIN(result, 1 - result);
        double winp = result - drawp / 2.0;
        double lossp = 1.0 - winp - drawp;

        double winpt = win_prob(se,eloH,eloD);
        double losspt = loss_prob(se,eloH,eloD);
        double drawpt = 1 - winpt - losspt;

        return  lossp * nlogp(losspt) +
                drawp * nlogp(drawpt) +
                winp  * nlogp(winpt);
    }
}
/*
Tunable parameter data type
*/
struct vPARAM{
    int* value;
    int size;
    int minv;
    int maxv;
    int flags;
    char name[64];
    vPARAM(int& x, int s, int f, 
           int mnv, int mxv,const char* n) {
        value = &x;
        size = s;
        flags = f;
        minv = mnv;
        maxv = mxv;
        strcpy(name,n);
    }
    bool is_pval() { return ((flags > 0) && !size); }
    bool is_pcsq() { return ((flags > 0) && size); }
    void set_zero() { *value = 0; }
    void set_randv() { *value = rand() % (maxv - minv + 1) + minv; }
};

/*define arrays of our parameters*/
static std::vector<vPARAM> parameters;
static std::vector<vPARAM> modelParameters;
static std::vector<vPARAM> inactive_parameters;
static int nParameters;
static int nModelParameters;
static int nInactiveParameters;
static float* jacobian = 0;
static float* jacobian_temp = 0;

/*
compute gradient of evaluation, stored in J for linear evaluation
*/
void allocate_jacobian(int npos) {
    size_t numbytes;
    numbytes = npos * (nParameters + nPadJac) * sizeof(float);
    jacobian = (float*)malloc(numbytes);
    if(jacobian) {
        print("Allocated jacobian matrix of size %.2f MB ...\n",
            double(numbytes)/(1024*1024));
    } else {
        print("Not enough memory.");
    }
}

static void compute_grad(PSEARCHER ps, float* J, double sc) {
    int sce,delta;
    vPARAM* p;

    for(int i = 0;i < nParameters;i++) {
        p = &parameters[i];
        delta = (p->maxv - p->minv) / 64;
        if(delta < 1) delta = 1;

        *(p->value) += delta;

        if(p->is_pval()) {
            int pic = p->flags;
            if(pic >= bking) ps->update_pcsq(pic-6,1,delta);
            else             ps->update_pcsq(pic+0,0,delta);
        } else if(p->is_pcsq()) {
            int pic = p->flags / 64 + 1;
            int sq = p->flags & 63;
            ps->update_pcsq_val(pic,sq,delta);
        }

        sce = ps->eval();

        *(p->value) -= delta;

        if(p->is_pval()) {
            int pic = p->flags;
            if(pic >= bking) ps->update_pcsq(pic-6,1,-delta);
            else             ps->update_pcsq(pic+0,0,-delta);
        } else if(p->is_pcsq()) {
            int pic = p->flags / 64 + 1;
            int sq = p->flags & 63;
            ps->update_pcsq_val(pic,sq,-delta);
        }

        J[i] = double(sce - sc) / delta;
    }
}
void compute_jacobian(PSEARCHER ps, int pos, double result) {
    float* J = jacobian + pos * (nParameters + nPadJac);
    double sc,se;

    sc = ps->eval();
    compute_grad(ps,J,sc);

    se = 0;
    for(int i = 0;i < nParameters;i++) {
        se += *(parameters[i].value) * J[i];
    }

    J[nParameters] = sc - se;
    J[nParameters+1] = result;
    J[nParameters+2] = material;
}
double eval_jacobian(int pos, double& result, double* params) {
    float* J = jacobian + pos * (nParameters + nPadJac);
    double se = J[nParameters];
    for(int i = 0;i < nParameters;i++) {
        se += params[i] * J[i];
    }

    result   = J[nParameters+1];
    material = (int) J[nParameters+2];
    return se;
}
void get_log_likelihood_grad(PSEARCHER ps, double result, double se, double* gse, int pos) {
    static const double reg_lambda = 1.0e-4;

    double nse, mse;
    mse = get_log_likelihood(result, se);

    float* J;
    if(jacobian) {
        J = jacobian + pos * (nParameters + nPadJac);
    } else {
        J = jacobian_temp;
        compute_grad(ps,J,se);
    }

    for(int i = 0;i < nParameters;i++) {
        vPARAM* p = &parameters[i];
        nse = se + J[i];
        gse[i] = (get_log_likelihood(result, nse) - mse)
#if 0
               + reg_lambda * ((*(p->value) > 0) ? 1 : (*(p->value) < 0));
#else
               + 2 * reg_lambda * ((*(p->value) - p->minv) / (p->maxv - p->minv));
#endif
    }

    for(int i = 0;i < nModelParameters;i++) {
        vPARAM* p = &modelParameters[i];
        int delta = (p->maxv - p->minv) / 64;
        if(delta < 1) delta = 1;

        *(p->value) += delta;
        gse[i + nParameters] = (get_log_likelihood(result, se) - mse) / delta
#if 0
               + reg_lambda * ((*(p->value) > 0) ? 1 : (*(p->value) < 0));
#else
               + 2 * reg_lambda * ((*(p->value) - p->minv) / (p->maxv - p->minv));
#endif
        *(p->value) -= delta;
    }
}
/*
read/write tunable parameters from/to array
*/
void readParams(double* params) {
    for(int i = 0;i < nParameters;i++)
        params[i] = *(parameters[i].value);
    for(int i = 0;i < nModelParameters;i++)
        params[i + nParameters] = *(modelParameters[i].value);
}
void writeParams(double* params) {
    for(int i = 0;i < nParameters;i++)
        *(parameters[i].value) = int(round(params[i]));
    for(int i = 0;i < nModelParameters;i++)
        *(modelParameters[i].value) = int(round(params[i + nParameters]));
}
/*
read/write tunable parameters to file
*/
static void write_eval_param(FILE* fd, vPARAM* p) {
    if(p->size == 0) {
        fprintf(fd,"static PARAM %s = %d;\n",
            p->name,*(p->value));
    } else {
        int nSize = p->size;
        fprintf(fd,"static PARAM %s[%d] = {\n",
            p->name,nSize);
        for(int j = 0;j < nSize;j++) {
            fprintf(fd,"%4d",*((p+j)->value));
            if(j < nSize - 1) {
                fprintf(fd,",");
                if((j + 1) % 8 == 0) 
                    fprintf(fd,"\n");
            } else fprintf(fd,"\n");
        }
        fprintf(fd,"};\n");
    }
}
void write_eval_params() {
    FILE* fd = fopen(PARAMS_FILE,"w");
    fprintf(fd,"//Automatically generated file. \n"
        "//Any changes made will be lost after tuning. \n");
    for(int i = 0;i < nParameters;i++) {
        write_eval_param(fd,&parameters[i]);
        int nSize = parameters[i].size;
        if(nSize) i += (nSize - 1);
    }
    for(int i = 0;i < nModelParameters;i++) {
        fprintf(fd,"static PARAM %s = %d;\n",
            modelParameters[i].name,*(modelParameters[i].value));
    }
    for(int i = 0;i < nInactiveParameters;i++) {
        write_eval_param(fd,&inactive_parameters[i]);
        int nSize = inactive_parameters[i].size;
        if(nSize) i += (nSize - 1);
    }
    fclose(fd);
}
void bound_params(double* params) {
    for(int i = 0;i < nParameters;i++) {
        if(params[i] < parameters[i].minv)
            params[i] = parameters[i].minv;
        if(params[i] > parameters[i].maxv)
            params[i] = parameters[i].maxv;
    }
}
void zero_params() {
    for(int i = 0;i < nParameters;i++)
        parameters[i].set_randv();
    for(int i = 0;i < nModelParameters;i++)
        modelParameters[i].set_randv();
    for(int i = 0;i < nInactiveParameters;i++)
        inactive_parameters[i].set_randv();
}
/*
update piece-square-tables
*/
void SEARCHER::update_pcsq(int pic, int eg, int dval) {
    for(int sq = 0; sq < 128; sq++) {
        if( ((sq & 0x88) && eg) || (!(sq & 0x88) && !eg) ) {
            pcsq[COMBINE(white,pic)][sq] += dval;
            pcsq[COMBINE(black,pic)][sq] += dval;
        }
    }
    if(eg) {
        pcsq_score[white].add(0, dval * man_c[COMBINE(white,pic)]);
        pcsq_score[black].add(0, dval * man_c[COMBINE(black,pic)]);
    } else {
        pcsq_score[white].add(dval * man_c[COMBINE(white,pic)], 0);
        pcsq_score[black].add(dval * man_c[COMBINE(black,pic)], 0);
    }
}
void SEARCHER::update_pcsq_val(int pic, int sq0, int dval) {
    PLIST current;
    int sq1,sq2;

    sq1 = SQ6488(sq0);
    if(file(sq1) >= FILEE) sq1 += 8;
    sq2 = MIRRORF(sq1);

    //white
    pcsq[COMBINE(white,pic)][sq1] += dval;
    pcsq[COMBINE(white,pic)][sq2] += dval;

    current = plist[COMBINE(white,pic)];
    while(current) {
        if(current->sq == sq1 || current->sq == sq2)
            pcsq_score[white].addm(dval);       
        else if(current->sq == sq1-8 || current->sq == sq2-8)
            pcsq_score[white].adde(dval);
        current = current->next;
    }

    sq1 = MIRRORR(sq1);
    sq2 = MIRRORR(sq2);

    //black
    pcsq[COMBINE(black,pic)][sq1] += dval;
    pcsq[COMBINE(black,pic)][sq2] += dval;

    current = plist[COMBINE(black,pic)];
    while(current) {
        if(current->sq == sq1 || current->sq == sq2)
            pcsq_score[black].addm(dval);       
        else if(current->sq == sq1-8 || current->sq == sq2-8)
            pcsq_score[black].adde(dval);
        current = current->next;
    }
}
/*
Initialize a subset of parameters
*/
void init_parameters(int group) {
    int actm = 0, actp = 0, actt = 0, acts = 0, acte = 0, actw = 0;

    /*select parameter group to optimize*/
    switch(group) {
        case 0: actm = actp = actt = acts = acte = 1; break;
        case 1: actm = actp = actt = acts = acte = actw = 1; break;
        case 2: actm = 1; break;
        case 3: actp = 1; break;
        case 4: actt = 1; break;
        case 5: acts = 1; break;
        case 6: acte = 1; break;
        case 7: actw = 1; break;
    }

#define ADD(x,ln,f,minv,maxv,act)                                   \
    if(act) parameters.push_back(vPARAM(x,ln,f,minv,maxv,#x));      \
    else inactive_parameters.push_back(vPARAM(x,ln,f,minv,maxv,#x));
#define ADDM(x,ln,f,minv,maxv,act)                                  \
    if(act) modelParameters.push_back(vPARAM(x,ln,f,minv,maxv,#x)); \
    else inactive_parameters.push_back(vPARAM(x,ln,f,minv,maxv,#x));
#define ADD_ARRAY(x,ln,f,minv,maxv,act)                             \
    for(int i = 0;i < ln; i++)                                      \
        if(act) parameters.push_back(vPARAM(x[i],ln,(f-1)*ln+i,minv,maxv,#x));       \
        else inactive_parameters.push_back(vPARAM(x[i],ln,(f-1)*ln+i,minv,maxv,#x));

    parameters.clear();
    inactive_parameters.clear();

    ADD(PAWN_GUARD,0,0,-128,256,actm);
    ADD(PAWN_STORM,0,0,-128,256,actm);
    ADD(KING_ON_OPEN,0,0,-128,256,actm);
    ADD(PAWN_ATTACK,0,0,-128,256,acts);
    ADD(KNIGHT_ATTACK,0,0,-128,256,acts);
    ADD(BISHOP_ATTACK,0,0,-128,256,acts);
    ADD(ROOK_ATTACK,0,0,-128,256,acts);
    ADD(QUEEN_ATTACK,0,0,-128,256,acts);
    ADD(UNDEFENDED_ATTACK,0,0,-128,256,actm);
    ADD(HANGING_PENALTY,0,0,-128,256,actm);
    ADD(ATTACKED_PIECE,0,0,-128,256,actm);
    ADD(DEFENDED_PIECE,0,0,-128,256,actm);
    ADD(KNIGHT_OUTPOST_MG,0,0,-128,256,actm);
    ADD(KNIGHT_OUTPOST_EG,0,0,-128,256,actm);
    ADD(BISHOP_OUTPOST_MG,0,0,-128,256,actm);
    ADD(BISHOP_OUTPOST_EG,0,0,-128,256,actm);
    ADD(KNIGHT_MOB_MG,0,0,-128,256,actm);
    ADD(KNIGHT_MOB_EG,0,0,-128,256,actm);
    ADD(BISHOP_MOB_MG,0,0,-128,256,actm);
    ADD(BISHOP_MOB_EG,0,0,-128,256,actm);
    ADD(ROOK_MOB_MG,0,0,-128,256,actm);
    ADD(ROOK_MOB_EG,0,0,-128,256,actm);
    ADD(QUEEN_MOB_MG,0,0,-128,256,actm);
    ADD(QUEEN_MOB_EG,0,0,-128,256,actm);
    ADD(BAD_BISHOP_MG,0,0,-128,256,actm);
    ADD(BAD_BISHOP_EG,0,0,-128,256,actm);
    ADD(MINOR_BEHIND_PAWN,0,0,-128,256,actm);
    ADD(PASSER_BLOCKED,0,0,-128,256,actm);
    ADD(PASSER_KING_SUPPORT,0,0,-128,256,actm);
    ADD(PASSER_KING_ATTACK,0,0,-128,256,actm);
    ADD(PASSER_SUPPORT,0,0,-128,256,actm);
    ADD(PASSER_ATTACK,0,0,-128,256,actm);
    ADD(PAWNS_VS_KNIGHT,0,0,-128,256,actm);
    ADD(CANDIDATE_PP_MG,0,0,-128,256,actm);
    ADD(CANDIDATE_PP_EG,0,0,-128,256,actm);
    ADD(PAWN_DOUBLED_MG,0,0,-128,256,actm);
    ADD(PAWN_DOUBLED_EG,0,0,-128,256,actm);
    ADD(PAWN_ISOLATED_ON_OPEN_MG,0,0,-128,256,actm);
    ADD(PAWN_ISOLATED_ON_OPEN_EG,0,0,-128,256,actm);
    ADD(PAWN_ISOLATED_ON_CLOSED_MG,0,0,-128,256,actm);
    ADD(PAWN_ISOLATED_ON_CLOSED_EG,0,0,-128,256,actm);
    ADD(PAWN_WEAK_ON_OPEN_MG,0,0,-128,256,actm);
    ADD(PAWN_WEAK_ON_OPEN_EG,0,0,-128,256,actm);
    ADD(PAWN_WEAK_ON_CLOSED_MG,0,0,-128,256,actm);
    ADD(PAWN_WEAK_ON_CLOSED_EG,0,0,-128,256,actm);
    ADD(PAWN_DUO_MG,0,0,-128,256,actm);
    ADD(PAWN_DUO_EG,0,0,-128,256,actm);
    ADD(PAWN_SUPPORTED_MG,0,0,-128,256,actm);
    ADD(PAWN_SUPPORTED_EG,0,0,-128,256,actm);
    ADD(ROOK_ON_7TH,0,0,-128,256,actm);
    ADD(ROOK_ON_OPEN,0,0,-128,256,actm);
    ADD(ROOK_SUPPORT_PASSED_MG,0,0,-128,256,actm);
    ADD(ROOK_SUPPORT_PASSED_EG,0,0,-128,256,actm);
    ADD(TRAPPED_BISHOP,0,0,0,512,actm);
    ADD(TRAPPED_KNIGHT,0,0,0,512,actm);
    ADD(TRAPPED_ROOK,0,0,0,512,actm);
    ADD(FRC_TRAPPED_BISHOP,0,0,0,512,actm);
    ADD(EXTRA_PAWN,0,0,0,512,actm);
    ADD(QUEEN_MG,0,wqueen,0,4096,actm);
    ADD(QUEEN_EG,0,bqueen,0,4096,actm);
    ADD(ROOK_MG,0,wrook,0,2048,actm);
    ADD(ROOK_EG,0,brook,0,2048,actm);
    ADD(BISHOP_MG,0,wbishop,0,2048,actm);
    ADD(BISHOP_EG,0,bbishop,0,2048,actm);
    ADD(KNIGHT_MG,0,wknight,0,2048,actm);
    ADD(KNIGHT_EG,0,bknight,0,2048,actm);
    ADD(PAWN_MG,0,wpawn,0,1024,actm);
    ADD(PAWN_EG,0,bpawn,0,1024,actm);
    ADD(BISHOP_PAIR_MG,0,0,-128,256,actm);
    ADD(BISHOP_PAIR_EG,0,0,-128,256,actm);
    ADD(MAJOR_v_P,0,0,-128,512,actm);
    ADD(MINOR_v_P,0,0,-128,512,actm);
    ADD(MINORS3_v_MAJOR,0,0,-128,512,actm);
    ADD(MINORS2_v_MAJOR,0,0,-128,512,actm);
    ADD(ROOK_v_MINOR,0,0,-128,512,actm);
    ADD(TEMPO_BONUS,0,0,-128,512,actm);
    ADD(TEMPO_SLOPE,0,0,-128,512,actm);
    ADD_ARRAY(king_pcsq,64,wking,-128,128,actp);
    ADD_ARRAY(queen_pcsq,64,wqueen,-128,128,actp);
    ADD_ARRAY(rook_pcsq,64,wrook,-128,128,actp);
    ADD_ARRAY(bishop_pcsq,64,wbishop,-128,128,actp);
    ADD_ARRAY(knight_pcsq,64,wknight,-128,128,actp);
    ADD_ARRAY(pawn_pcsq,64,wpawn,-128,128,actp);
    ADD_ARRAY(outpost,32,0,-128,128,actt);
    ADD_ARRAY(passed_rank_bonus,8,0,-128,512,actt);
    ADD_ARRAY(passed_file_bonus,4,0,-128,256,actt);
    ADD_ARRAY(qr_on_7thrank,18,0,-128,512,actt);
    ADD_ARRAY(rook_on_hopen,13,0,-128,256,actt);
    ADD_ARRAY(king_to_pawns,8,0,-128,256,actt);
    ADD_ARRAY(king_on_hopen,8,0,-128,256,actt);
    ADD_ARRAY(king_on_file,8,0,-128,256,actt);
    ADD_ARRAY(king_on_rank,8,0,-128,256,actt);
    ADD_ARRAY(piece_tropism,8,0,-128,256,actt);
    ADD_ARRAY(queen_tropism,8,0,-128,256,actt);
    ADD_ARRAY(file_tropism,8,0,-128,256,actt);
    ADDM(ELO_DRAW,0,0,-128,512,acte);
    ADDM(ELO_DRAW_SLOPE_PHASE,0,0,-128,512,acte);
    ADD(PASSER_WEIGHT_MG,0,0,-128,256,actw);
    ADD(PASSER_WEIGHT_EG,0,0,-128,256,actw);
    ADD(ATTACK_WEIGHT,0,0,-128,256,actw);
    ADD(TROPISM_WEIGHT,0,0,-128,256,actw);

#undef ADD
#undef ADDM
#undef ADD_ARRAY
    nParameters = parameters.size();
    nModelParameters = modelParameters.size();
    nInactiveParameters = inactive_parameters.size();
    
    if(jacobian_temp) free(jacobian_temp);
    jacobian_temp = (float*) malloc(nParameters * sizeof(float));
}
/*
print/check evaluation commands
*/
void print_eval_params() {
    for(int i = 0;i < nParameters;i++) {
        print_spin(
            parameters[i].name,*(parameters[i].value),
            parameters[i].minv,parameters[i].maxv);
    }
    for(int i = 0;i < nModelParameters;i++) {
        print_spin(
            modelParameters[i].name,*(modelParameters[i].value),
            modelParameters[i].minv,modelParameters[i].maxv);
    }
    print_spin("elo_model",ELO_MODEL,0,2);
    print_spin("lr_factor",LR_FACTOR,0,1000);
}
bool check_eval_params(char** commands,char* command,int& command_num) {
    for(int i = 0;i < nParameters;i++) {
        if(!strcmp(command, parameters[i].name)) {
            *(parameters[i].value) = atoi(commands[command_num++]);
            return true;
        }
    }
    for(int i = 0;i < nModelParameters;i++) {
        if(!strcmp(command, modelParameters[i].name)) {
            *(modelParameters[i].value) = atoi(commands[command_num++]);
            return true;
        }
    }
    if(!strcmp(command, "elo_model")) {
        ELO_MODEL = atoi(commands[command_num++]);
    } else if(!strcmp(command, "lr_factor")) {
        LR_FACTOR = atoi(commands[command_num++]);
    } else if(!strcmp(command,"param_group")) {
        int parameter_group = atoi(commands[command_num++]);
        init_parameters(parameter_group);
    } else if(!strcmp(command,"zero_params")) {
        zero_params();
        write_eval_params();
    } else {
        return false;
    }
    return true;
}
/*
Tuner
*/
void SEARCHER::tune(int task, char* path) {
    static EPD tune_epd_file;
    static const int MINI_BATCH_SIZE = 4096;
    static const int VALID_BATCH_SIZE = MINI_BATCH_SIZE >> 3;
    static const int WRITE_FREQ = 10;
    static const double momentum = 0.9;
    static const double lr_schedule[] = {500.0, 250.0, 125.0, 62.5};
    static const double lr_schedule_steps[] = {0.25, 0.5, 0.75, 1.0};

    int lr_idx = 0;
    double alpha = lr_schedule[lr_idx] * LR_FACTOR / 100;
    enum {JACOBIAN=0, TUNE_EVAL};

    /*open file if not already open*/
    char fen[4 * MAX_FILE_STR];
    bool getfen = ((task == JACOBIAN) || (task == TUNE_EVAL && !jacobian));

    if(getfen && !tune_epd_file.is_open())
        tune_epd_file.open(path,true);

    /*number of fens in file*/
    static int n_fens = 0;
    if(!n_fens) {
        while(tune_epd_file.next(fen,true)) {
            n_fens++;
        }
        tune_epd_file.rewind();
    }

    /*allocate jacobian*/
    if(task == JACOBIAN) {
        allocate_jacobian(n_fens);
        print("Computing jacobian matrix of evaluation function ...\n");
    }

    /*allocate arrays for SGD*/
    double *gse, *gmse, *dmse, *params, *vgmse;
    int nSize = nParameters + nModelParameters;

    if(task == TUNE_EVAL) {
        gse = (double*) malloc(nSize * sizeof(double));
        gmse = (double*) malloc(nSize * sizeof(double));
        dmse = (double*) malloc(nSize * sizeof(double));
        params = (double*) malloc(nSize * sizeof(double));
        vgmse = (double*) malloc(nSize * sizeof(double));
        memset(dmse,0,nSize * sizeof(double));
        memset(gmse,0,nSize * sizeof(double));
        memset(vgmse,0,nSize * sizeof(double));
        readParams(params);
    }

    /*initialize*/
    SEARCHER::pre_calculate();

    /*loop through all positions*/
    int visited = 0;
    int minibatch = 0;
    double result;
    double normg_t = 0, vnormg_t = 0;

    for(int cnt = 0;cnt < n_fens;cnt++) {

        visited++;

        /*read line and parse fen*/
        if(getfen) {
            if(!tune_epd_file.next(fen,true))
                break;

            fen[strlen(fen) - 1] = 0;
            set_board(fen);
            SEARCHER::scorpio = player;

            result = 2;
            if(strstr(fen, "1-0") != NULL)
                result = 1;
            else if(strstr(fen, "0-1") != NULL)
                result = 0;
            else if(strstr(fen, "1/2-1/2") != NULL)
                result = 0.5;

            if(result > 1.5) {
                char *p = strrchr(fen, ' ');
                if (p && *(p + 1)) {
#if 0
                    int score;
                    sscanf(p + 1,"%d",&score);
                    if(player == black)
                        score = -score;
                    result = logistic(score);
#else
                    sscanf(p,"%lf",&result);
#endif
                } else {
                    print("Position %d not labeled: fen %s\n",
                        minibatch * MINI_BATCH_SIZE + visited, fen);
                    visited--;
                    continue;
                }
            }

            if(player == black)
                result = 1 - result;
        }

        /*job*/
        if(task == JACOBIAN) {
            compute_jacobian(this,cnt,result);
        } else {
            /*compute evaluation from the stored jacobian*/
            double se;
            if(getfen) {
                se = eval();
            } else {
                se = eval_jacobian(cnt,result,params);
            }
            /*compute loss function (log-likelihood) or its gradient*/
            get_log_likelihood_grad(this,result,se,gse,cnt);
            for(int i = 0;i < nSize;i++)
                gmse[i] += (gse[i] - gmse[i]) / visited;
            /*validation data*/
            if(visited > VALID_BATCH_SIZE) {
                for(int i = 0;i < nSize;i++)
                    vgmse[i] += (gse[i] - vgmse[i]) / (visited - VALID_BATCH_SIZE);
            }
        }

        /*update parameters with SGD + momentum*/
        if(task == TUNE_EVAL && visited == MINI_BATCH_SIZE) {
            minibatch++;

            double normg = 0, vnormg = 0;
            for(int i = 0;i < nSize;i++) {
                dmse[i] = momentum * dmse[i] + (1 - momentum) * gmse[i];
                params[i] -= alpha * dmse[i];
                normg += pow(gmse[i],2.0);
                vnormg += pow(vgmse[i],2.0);
            }
            normg_t += (normg - normg_t) / minibatch;
            vnormg_t += (vnormg - vnormg_t) / minibatch;

            bound_params(params);
            writeParams(params);
            if(minibatch % WRITE_FREQ == 0)
                write_eval_params();

            print("%8d. Training |R|=%.6e Validation |R|=%.6e LR=%.1f\n",
                minibatch, normg_t, vnormg_t, alpha);

            if(getfen)
                SEARCHER::pre_calculate();

            /*reset*/
            visited = 0;
            memset(gmse,0,nSize * sizeof(double));
            memset(vgmse,0,nSize * sizeof(double));

            /*adjust learning rate*/
            if(minibatch * MINI_BATCH_SIZE > 
                lr_schedule_steps[lr_idx] * n_fens) {
                alpha = lr_schedule[++lr_idx] * LR_FACTOR / 100.0;
            }
        }
    }

    /*finish*/
    if(task == TUNE_EVAL) {
        free(gse);
        free(gmse);
        free(dmse);
        free(params);
        free(vgmse);
        tune_epd_file.close();
    }
    new_board();
}
#endif
