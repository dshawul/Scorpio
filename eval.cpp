#include "scorpio.h"
 
/*
masks
*/
static const UBMP8  mask[8] = {
	1,  2,  4,  8, 16, 32, 64,128
};
static const UBMP8  up_mask[8] = {
	254,252,248,240,224,192,128,  0
};
static const UBMP8  down_mask[8] = {
	0,  1,  3,  7, 15, 31, 63,127
};
static const UBMP8  updown_mask[8] = {
	254, 253, 251, 247, 239, 223, 191, 127
};
/*
eval terms
*/
static const int outpost[] = { 
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,  8, 10, 10,  8,	0,	0,
	0,	0, 10, 16, 16, 10,	0,	0,
	0,	0,  8, 10, 10,  8,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0
}; 
static const int passed_bonus[] = {
	0, 8, 14, 23, 45, 83, 113, 0
};
static const int qr_on_7thrank[] = {
	0,  0,  30, 20, 80, 100, 200, 200, 200,
	200, 200, 200, 200, 200, 200, 200, 200, 200
};
static const int rook_on_hopen[] = {
	0,  10,  20,  30,  50, 60, 70, 90, 100, 110,
	130, 140, 150
};
static const int king_to_pawns[] = {
	0,  20,  60,  100, 140, 140, 140, 140
};
static const int king_on_hopen[] = {
	0,  1,  4,  6,  4,  6,  7,  7
};
static const int king_on_file[] = {
	2,  1,  3,  4,  4,  2,  0,  1
};
static const int king_on_rank[] = {
	0,  1,  2,  3,  3,  3,  3,  3
};
static const int  piece_tropism[8] = {
	0,  5, 15, 10,  5,  0,  0,  0
};
static const int  queen_tropism[8] = {
	0,  0, 20, 20, 15,  5,  0,  0
};
static const int  file_tropism[8] = {
	25, 25, 10, 5,  0,  0,  0,  0
};
/*
* Tunable parameters
*/
#ifdef TUNE
#	define PARAM
#else
#	define PARAM const
#endif

static PARAM int LAZY_MARGIN_MIN = 150;
static PARAM int LAZY_MARGIN_MAX = 300;
static PARAM int ATTACK_WEIGHT = 16;
static PARAM int TROPISM_WEIGHT = 8;
static PARAM int PAWN_GUARD = 16;
static PARAM int KNIGHT_OUTPOST = 16;
static PARAM int BISHOP_OUTPOST = 12;
static PARAM int KNIGHT_MOB = 16;
static PARAM int BISHOP_MOB = 16;
static PARAM int ROOK_MOB = 16;
static PARAM int QUEEN_MOB = 16;
static PARAM int PASSER_MG = 12;
static PARAM int PASSER_EG = 16;
static PARAM int PAWN_STRUCT_MG = 12;
static PARAM int PAWN_STRUCT_EG = 16;
static PARAM int ROOK_ON_7TH = 12;
static PARAM int ROOK_ON_OPEN = 16;
static PARAM int QUEEN_MG = 1050;
static PARAM int QUEEN_EG = 1050;
static PARAM int ROOK_MG = 500;
static PARAM int ROOK_EG = 500;
static PARAM int BISHOP_MG = 325;
static PARAM int BISHOP_EG = 325;
static PARAM int KNIGHT_MG = 325;
static PARAM int KNIGHT_EG = 325;
static PARAM int PAWN_MG = 80;
static PARAM int PAWN_EG = 100;
static PARAM int BISHOP_PAIR_MG = 16;
static PARAM int BISHOP_PAIR_EG = 16;
static PARAM int EXCHANGE_BONUS = 45;
/*
king safety
*/
static int KING_ATTACK(int attack,int attackers,int tropism) {
	if(attackers == 0) return 0;
	int score = ((9 * ATTACK_WEIGHT * attack + 1 * TROPISM_WEIGHT * tropism) >> 4);
	if(attackers == 1) return (score >> 2);
	int geometric = 2 << (attackers - 2);
	return ((score) * (geometric - 1)) / geometric;
}
/*
static evaluator
*/
#define RETURN() {                 \
	return pstack->actual_score;   \
}

/*max material count is 64 {actually 62}*/
#define   MAX_MATERIAL    64

int SEARCHER::eval(int lazy) {
	register SCORE w_score,b_score;
	register int w_ksq = plist[wking]->sq;
    register int b_ksq = plist[bking]->sq;
	int w_win_chance = 8,b_win_chance = 8;
	int phase = min(MAX_MATERIAL,piece_c[white] + piece_c[black]);
	int temp;
	
	/*reset*/
	memset(&pstack->evalrec,0,sizeof(pstack->evalrec));
  
	/*
	evaluate winning chances for some endgames.
	*/
	eval_win_chance(w_score,b_score,w_win_chance,b_win_chance);

	/*trapped bishop*/
	if((board[A7] == wbishop || (board[B8] == wbishop && board[C7] == bpawn))
		&& board[B6] == bpawn
		) {
		w_score.sub(80);
		if(board[C7] == bpawn)
			w_score.sub(40);
	}
    if((board[H7] == wbishop || (board[G8] == wbishop && board[F7] == bpawn))
		&& board[G6] == bpawn
		) {
		w_score.sub(80);
		if(board[F7] == bpawn) 
			w_score.sub(40);
	}
	if((board[A2] == bbishop || (board[B1] == bbishop && board[C2] == wpawn))
		&& board[B3] == wpawn
		) {
		b_score.sub(80);
		if(board[C2] == wpawn)
			b_score.sub(40);
	}
    if((board[H2] == bbishop || (board[G1] == bbishop && board[F2] == wpawn))
		&& board[G3] == wpawn
		) {
		b_score.sub(80);
		if(board[F2] == wpawn) 
			b_score.sub(40);
	}

	/*trapped bishop at A6/H6/A3/H3*/
	if(board[A6] == wbishop && board[B5] == bpawn)
		w_score.sub(50);
	if(board[H6] == wbishop && board[G5] == bpawn)
		w_score.sub(50);
	if(board[A3] == bbishop && board[B4] == wpawn)
		b_score.sub(50);
	if(board[H3] == bbishop && board[G4] == wpawn)
		b_score.sub(50);

	/*trapped knight*/
	if(board[A7] == wknight && board[B7] == bpawn && (board[C6] == bpawn || board[A6] == bpawn))
		w_score.sub(50);
    if(board[H7] == wknight && board[G7] == bpawn && (board[F6] == bpawn || board[H6] == bpawn)) 
		w_score.sub(50);
	if(board[A2] == bknight && board[B2] == wpawn && (board[C3] == wpawn || board[A3] == wpawn)) 
		b_score.sub(50);
    if(board[H2] == bknight && board[G2] == wpawn && (board[F3] == wpawn || board[H3] == wpawn)) 
		b_score.sub(50);

	/*trapped knight at A8/H8/A1/H1*/
	if(board[A8] == wknight && (board[A7] == bpawn || board[C7] == bpawn))
		w_score.sub(50);
	if(board[H8] == wknight && (board[H7] == bpawn || board[G7] == bpawn))
		w_score.sub(50);
	if(board[A1] == bknight && (board[A2] == wpawn || board[C2] == wpawn))
		b_score.sub(50);
	if(board[H1] == bknight && (board[H2] == wpawn || board[G2] == wpawn))
		b_score.sub(50);

	/*trapped rook*/
	if((board[F1] == wking || board[G1] == wking) && 
		(board[H1] == wrook || board[H2] == wrook || board[G1] == wrook))
		w_score.subm(90);
	if((board[C1] == wking || board[B1] == wking) && 
		(board[A1] == wrook || board[A2] == wrook || board[B1] == wrook))
		w_score.subm(90);
	if((board[F8] == bking || board[G8] == bking) && 
		(board[H8] == brook || board[H7] == brook || board[G8] == brook))
		b_score.subm(90);
	if((board[C8] == bking || board[B8] == bking) && 
		(board[A8] == brook || board[A7] == brook || board[B8] == brook))
		b_score.subm(90);
	/*
	lazy evaluation - 1
	*/
	if(eval_lazy_exit(lazy,LAZY_MARGIN_MAX,phase,
		w_score,b_score,w_win_chance,b_win_chance))
		RETURN();
	/*
	check_eval hash table
	*/
	if(probe_eval_hash(hash_key,pstack->actual_score,
	                   pstack->lazy_score,pstack->evalrec)) {
	    full_evals++;
		if(player == black) {
			pstack->actual_score = -pstack->actual_score;
			pstack->lazy_score = -pstack->lazy_score;
		}
		RETURN();
	}
	/*
	pawns
	*/
	UBMP8 all_pawn_f;
	BITBOARD _wps = Rotate(pawns_bb[white]);
	BITBOARD _bps = Rotate(pawns_bb[black]);
	UBMP8* wf_pawns = (UBMP8*) (&_wps);
	UBMP8* bf_pawns = (UBMP8*) (&_bps);
	int eval_w_attack = (man_c[wqueen] && piece_c[white] > 9);
	int eval_b_attack = (man_c[bqueen] && piece_c[black] > 9);

	w_score.add(eval_pawns(eval_w_attack,eval_b_attack,wf_pawns,bf_pawns));
	all_pawn_f = pawnrec.w_pawn_f | pawnrec.b_pawn_f;

	/*passed pawns*/
	if(pawnrec.w_passed | pawnrec.b_passed) {
		temp = eval_passed_pawns(wf_pawns,bf_pawns,all_pawn_f);
		w_score.add((PASSER_MG * temp) / 16,(PASSER_EG * temp) / 16); 
	}

	/*king attack/defence*/
	if(eval_b_attack) {
		b_score.addm((PAWN_GUARD * pawnrec.b_s_attack) / 16);
	}
	if(eval_w_attack) {
		w_score.addm((PAWN_GUARD * pawnrec.w_s_attack) / 16);
	}
	/*
	lazy evaluation - 2
	*/
	if(eval_lazy_exit(lazy,LAZY_MARGIN_MIN,phase,
		w_score,b_score,w_win_chance,b_win_chance))
		RETURN();
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
	register BITBOARD bb;
	BITBOARD noccupancyw = ~(pieces_bb[white] | pawns_bb[white]);
	BITBOARD noccupancyb = ~(pieces_bb[black] | pawns_bb[black]);
	BITBOARD occupancy = (~noccupancyw | ~noccupancyb);
	BITBOARD wk_bb = king_attacks(SQ8864(w_ksq));
	BITBOARD bk_bb = king_attacks(SQ8864(b_ksq));
	wk_bb |= (wk_bb << 8);
	bk_bb |= (bk_bb >> 8);

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

		mob = (3 * KNIGHT_MOB * (popcnt(bb & noccupancyw) - 4)) / 16;
		w_score.add(mob);

		/*attack*/
        if(eval_w_attack) {
			w_tropism += piece_tropism[distance(c_sq, b_ksq)];
			bb &= bk_bb;
			if(bb) {
				w_attack += popcnt_sparse(bb);
				w_attackers++;
			}
		}

		/*knight outpost*/
		if((temp = ((KNIGHT_OUTPOST * outpost[sq]) / 16))
			&& (f == FILEA || !(up_mask[r] & bf_pawns[f - 1]))
			&& (f == FILEH || !(up_mask[r] & bf_pawns[f + 1]))
			) {
				opost = temp;
				if(!(up_mask[r] & bf_pawns[f])) {
					if(board[c_sq + LD] == wpawn) opost += temp;
					if(board[c_sq + RD] == wpawn) opost += temp;
				} else {
					if(board[c_sq + LD] == wpawn || board[c_sq + RD] == wpawn) 
						opost += temp;
				}
				w_score.add(opost,opost / 2);
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

		mob = (3 * KNIGHT_MOB * (popcnt(bb & noccupancyb) - 4)) / 16;
		b_score.add(mob);

		/*attack*/
		if(eval_b_attack) {
			b_tropism += piece_tropism[distance(c_sq, w_ksq)];
			bb &= wk_bb;
			if(bb) {
				b_attack += popcnt_sparse(bb);
				b_attackers++;
			}
		}
		/*knight outpost*/
		if((temp = ((KNIGHT_OUTPOST * outpost[MIRRORR64(sq)]) / 16))
			&& (f == FILEA || !(down_mask[r] & wf_pawns[f - 1]))
			&& (f == FILEH || !(down_mask[r] & wf_pawns[f + 1]))
			) {
				opost = temp;
				if(!(down_mask[r] & wf_pawns[f])) {
					if(board[c_sq + LU] == bpawn) opost += temp;
					if(board[c_sq + RU] == bpawn) opost += temp;
				} else {
					if(board[c_sq + LU] == bpawn || board[c_sq + RU] == bpawn) 
						opost += temp;
				}
				b_score.add(opost,opost / 2);
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

		mob = (5 * BISHOP_MOB * (popcnt(bb & noccupancyw) - 6)) / 16;
		w_score.add(mob);

		/*attack*/
		if(eval_w_attack) {
			w_tropism += piece_tropism[distance(c_sq, b_ksq)];
			bb &= bk_bb;
			if(bb) {
				w_attack += popcnt_sparse(bb);
				w_attackers++;
			}
		}

		/*bishop outpost*/
		if((temp = ((BISHOP_OUTPOST * outpost[sq]) / 16))
			&& (f == FILEA || !(up_mask[r] & bf_pawns[f - 1]))
			&& (f == FILEH || !(up_mask[r] & bf_pawns[f + 1]))
			) {
				opost = temp;
				if(!(up_mask[r] & bf_pawns[f])) {
					if(board[c_sq + LD] == wpawn) opost += temp;
					if(board[c_sq + RD] == wpawn) opost += temp;
				} else {
					if(board[c_sq + LD] == wpawn || board[c_sq + RD] == wpawn) 
						opost += temp;
				}
				w_score.add(opost,opost / 2);
		}
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

		mob = (5 * BISHOP_MOB * (popcnt(bb & noccupancyb) - 6)) / 16;
		b_score.add(mob);

		/*attack*/
		if(eval_b_attack) {
			b_tropism += piece_tropism[distance(c_sq, w_ksq)];
			bb &= wk_bb;
			if(bb) {
				b_attack += popcnt_sparse(bb);
				b_attackers++;
			}
		}

		/*bishop outpost*/
		if((temp = ((BISHOP_OUTPOST * outpost[MIRRORR64(sq)]) / 16))
			&& (f == FILEA || !(down_mask[r] & wf_pawns[f - 1]))
			&& (f == FILEH || !(down_mask[r] & wf_pawns[f + 1]))
			) {
				opost = temp;
				if(!(down_mask[r] & wf_pawns[f])) {
					if(board[c_sq + LU] == bpawn) opost += temp;
					if(board[c_sq + RU] == bpawn) opost += temp;
				} else {
					if(board[c_sq + LU] == bpawn || board[c_sq + RU] == bpawn) 
						opost += temp;
				}
				b_score.add(opost,opost / 2);
		}
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

		mob = (3 * ROOK_MOB * (popcnt(bb & noccupancyw) - 7)) / 16;
		w_score.add(mob);

		/*attack*/
		if(eval_w_attack) {
			w_tropism += piece_tropism[distance(c_sq, b_ksq)];
			bb &= bk_bb;
			if(bb) {
				w_attack += (popcnt_sparse(bb) * 2);
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
				w_score.add(10,20);
		}
		if(pawnrec.b_passed & mask[f]) {
			if(r > first_bit[bf_pawns[f]])
				w_score.add(10,20);
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

		mob = (3 * ROOK_MOB * (popcnt(bb & noccupancyb) - 7)) / 16;
		b_score.add(mob);

		/*attack*/
		if(eval_b_attack) {
			b_tropism += piece_tropism[distance(c_sq, w_ksq)];
			bb &= wk_bb;
			if(bb) {
				b_attack += (popcnt_sparse(bb) * 2);
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
				b_score.add(10,20);
		}
		if(pawnrec.w_passed & mask[f]) {
			if(r < last_bit[wf_pawns[f]])
				b_score.add(10,20);
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

		sq = SQ64(r,f);
	    bb =  queen_attacks(sq,occupancy);

		mob = (QUEEN_MOB * (popcnt(bb & noccupancyw) - 13)) / 16;
		w_score.add(mob);

		/*attack*/
		if(eval_w_attack) {
			w_tropism += queen_tropism[distance(c_sq, b_ksq)];
			bb &= bk_bb;
			if(bb) {
				w_attack += (popcnt_sparse(bb) * 4);
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
       
		sq = SQ64(r,f);
		bb =  queen_attacks(sq,occupancy);

		mob = (QUEEN_MOB * (popcnt(bb & noccupancyb) - 13)) / 16;
		b_score.add(mob);

		/*attack*/
		if(eval_b_attack) {
			b_tropism += queen_tropism[distance(c_sq, w_ksq)]; 
			bb &= wk_bb;
			if(bb) {
				b_attack += (popcnt_sparse(bb) * 4);
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
	if(eval_b_attack) {
		b_attack += pawnrec.b_attack;
		if(b_attack >= 18) pstack->evalrec.indicator |= BAD_WKING;
		b_score.addm(KING_ATTACK(b_attack,b_attackers,b_tropism));
	}
	if(eval_w_attack) {
		w_attack += pawnrec.w_attack;
		if(w_attack >= 18) pstack->evalrec.indicator |= BAD_BKING;
		w_score.addm(KING_ATTACK(w_attack,w_attackers,w_tropism));
	}
	/*
	adjust score and save in tt
	*/
	full_evals++;

	if(player == white) {
		pstack->actual_score = ((w_score.mid - b_score.mid) * (phase) +
			                   (w_score.end - b_score.end) * (MAX_MATERIAL - phase)) / MAX_MATERIAL;
		if(pstack->actual_score > 0) {
			pstack->actual_score = (pstack->actual_score * w_win_chance) / 8;
		} else {
			pstack->actual_score = (pstack->actual_score * b_win_chance) / 8;
		}
		record_eval_hash(hash_key,pstack->actual_score, pstack->lazy_score ,pstack->evalrec);
	} else {
		pstack->actual_score = ((b_score.mid - w_score.mid) * (phase) +
			                   (b_score.end - w_score.end) * (MAX_MATERIAL - phase)) / MAX_MATERIAL;
		if(pstack->actual_score > 0) {
			pstack->actual_score = (pstack->actual_score * b_win_chance) / 8;
		} else {
			pstack->actual_score = (pstack->actual_score * w_win_chance) / 8;
		}
		record_eval_hash(hash_key,-pstack->actual_score,-pstack->lazy_score ,pstack->evalrec);
	}
    
	RETURN();
}
/*
piece square tables - middle/endgame 
*/
static int  king_pcsq[0x80] = {
 -7,  0,-15,-20,-20,-15,  0, -7,        -30,-20,-10,  0,  0,-10,-20,-30,
-17,-17,-25,-30,-30,-25,-17,-17,        -20,-10,  0, 10, 10,  0,-10,-20,
-40,-40,-40,-40,-40,-40,-40,-40,        -10,  0, 10, 20, 20, 10,  0,-10,
-60,-60,-60,-60,-60,-60,-60,-60,          0, 10, 20, 30, 30, 20, 10,  0,
-70,-80,-80,-80,-80,-80,-80,-70,          0, 10, 20, 30, 30, 20, 10,  0,
-80,-80,-80,-80,-80,-80,-80,-80,        -10,  0, 10, 20, 20, 10,  0,-10,
-80,-80,-80,-80,-80,-80,-80,-80,        -20,-10,  0, 10, 10,  0,-10,-20,
-80,-80,-80,-80,-80,-80,-80,-80,        -30,-20,-10,  0,  0,-10,-20,-30,
};
static int  queen_pcsq[0x80] = {
 -5, -5, -5, -5, -5, -5, -5, -5,	     -5, -5, -5, -5, -5, -5, -5, -5,          
 -5,  0,  0,  0,  0,  0,  0, -5,	     -5,  0,  0,  0,  0,  0,  0, -5,          
 -5,  0,  0,  0,  0,  0,  0, -5,	     -5,  0,  4,  4,  4,  4,  0, -5,          
 -5,  0,  0,  0,  0,  0,  0, -5,	     -5,  0,  4,  8,  8,  4,  0, -5,          
 -5,  0,  0,  0,  0,  0,  0, -5,	    -10,  0,  4,  8,  8,  4,  0,-10,          
 -5,  0,  0,  0,  0,  0,  0, -5,	    -20,  0,  4,  4,  4,  4,  0,-20,          
 -5,  0,  0,  0,  0,  0,  0, -5,	    -20, -5, -5, -5, -5, -5, -5,-20,          
 -5, -5, -5, -5, -5, -5, -5, -5,	    -20,-20,-10,-10,-10,-10,-20,-20,          
};
static int  rook_pcsq[0x80] = { 
 -5,  0,  2,  5,  5,  2,  0, -5,          0,  0,  0,  0,  0,  0,  0,  0,
 -5,  0,  2,  5,  5,  2,  0, -5,          0,  0,  0,  0,  0,  0,  0,  0,
 -5,  0,  2,  5,  5,  2,  0, -5,          0,  0,  0,  0,  0,  0,  0,  0,
 -5,  0,  2,  5,  5,  2,  0, -5,          0,  0,  0,  0,  0,  0,  0,  0,
 -5,  0,  2,  5,  5,  2,  0, -5,          0,  0,  0,  0,  0,  0,  0,  0,
 -5,  0,  2,  5,  5,  2,  0, -5,          0,  0,  0,  0,  0,  0,  0,  0,
 -5,  0,  2,  5,  5,  2,  0, -5,          0,  0,  0,  0,  0,  0,  0,  0,
 -5,  0,  2,  5,  5,  2,  0, -5,          0,  0,  0,  0,  0,  0,  0,  0,
};
static int  bishop_pcsq[0x80] = {
-15,-20,-20,-20,-20,-20,-20,-15,        -15,-20,-20,-20,-20,-20,-20,-15,
-10,  0, -5, -5, -5, -5,  0,-10,        -10,  0, -5, -5, -5, -5,  0,-10,
-10, -5,  5,  0,  0,  5, -5,-10,        -10, -5,  5,  0,  0,  5, -5,-10,
-10, -5,  0, 10, 10,  0, -5,-10,        -10, -5,  0, 10, 10,  0, -5,-10,
-15, -5,  0, 10, 10,  0, -5,-15,        -15, -5,  0, 10, 10,  0, -5,-15,
-10, -5,  5,  0,  0,  5, -5,-10,        -10, -5,  5,  0,  0,  5, -5,-10,
-10,-10, -5, -5, -5, -5,-10,-10,        -10,-10, -5, -5, -5, -5,-10,-10,
-10,-10,-10,-10,-10,-10,-10,-10,        -10,-10,-10,-10,-10,-10,-10,-10,
};
static int  knight_pcsq[0x80] = { 
-40,-30,-20,-20,-20,-20,-30,-40,        -30,-23,-15,-15,-15,-15,-23,-30,
-30, -5, -5, -5, -5, -5, -5,-30,        -23, -4, -4, -4, -4, -4, -4,-23,
-10, -5,  5, 10, 10,  5, -5,-10,         -8, -4,  3,  7,  7,  3, -4, -8,
-10, -5, 10, 15, 15, 10, -5,-10,         -8, -4,  7, 11, 11,  7, -4, -8,
-15, -5, 10, 15, 15, 10, -5,-15,        -12, -4,  7, 11, 11,  7, -4,-12,
-15, -5,  5, 10, 10,  5, -5,-15,        -12, -4,  3,  7,  7,  3, -4,-12,
-30,-10,-10,-10,-10,-10,-10,-30,        -23, -8, -8, -8, -8, -8, -8,-23,
-40,-30,-20,-20,-20,-20,-30,-40,        -30,-23,-15,-15,-15,-15,-23,-30,
};
static int  pawn_pcsq[0x80] = { 
  0,  0,  0,  0,  0,  0,  0,  0,          0,  0,  0,  0,  0,  0,  0,  0,
 -8,  0,  7, 14, 14,  7,  0, -8,          0,  0,  0,  0,  0,  0,  0,  0,
 -8,  7, 14, 24, 24,  7,  7, -8,          0,  0,  0,  0,  0,  0,  0,  0,
 -8, 11, 15, 35, 35, 15, 11, -8,          0,  0,  0,  0,  0,  0,  0,  0,
 -8, 11, 15, 20, 20, 15, 11, -8,          0,  0,  0,  0,  0,  0,  0,  0,
 -8, 11, 15, 20, 20, 15, 11, -8,          0,  0,  0,  0,  0,  0,  0,  0,
 -8, 11, 15, 20, 20, 15, 11, -8,          0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,          0,  0,  0,  0,  0,  0,  0,  0,
};
void SEARCHER::pre_calculate() {
	int piece_value[] = {
		0,0,QUEEN_MG,ROOK_MG,BISHOP_MG,KNIGHT_MG,PAWN_MG,0,QUEEN_MG,ROOK_MG,BISHOP_MG,KNIGHT_MG,PAWN_MG,0,
		0,0,QUEEN_EG,ROOK_EG,BISHOP_EG,KNIGHT_EG,PAWN_EG,0,QUEEN_EG,ROOK_EG,BISHOP_EG,KNIGHT_EG,PAWN_EG,0,
	};
	int pic,sq;
	for(pic = wking; pic <= bpawn; pic++) {
		for(sq = 0; sq <= 128; sq++) {
			if(sq & 0x88) pcsq[pic][sq] = piece_value[pic + 14];
			else pcsq[pic][sq] = piece_value[pic];
		}
	}
	for(sq = 0; sq < 128; sq++) {
        pcsq[wking][sq] += king_pcsq[sq];
		pcsq[bking][sq] += king_pcsq[MIRRORR(sq)];
		pcsq[wknight][sq] += knight_pcsq[sq];
		pcsq[bknight][sq] += knight_pcsq[MIRRORR(sq)];
		pcsq[wbishop][sq] += bishop_pcsq[sq];
		pcsq[bbishop][sq] += bishop_pcsq[MIRRORR(sq)];
		pcsq[wrook][sq] += rook_pcsq[sq];
		pcsq[brook][sq] += rook_pcsq[MIRRORR(sq)];
		pcsq[wqueen][sq] += queen_pcsq[sq];
		pcsq[bqueen][sq] += queen_pcsq[MIRRORR(sq)];
		pcsq[wpawn][sq] += pawn_pcsq[sq];
		pcsq[bpawn][sq] += pawn_pcsq[MIRRORR(sq)];
	}
}
/*
evaluate pawn cover
*/
void SEARCHER::eval_pawn_cover(int eval_w_attack,int eval_b_attack,
						 UBMP8* wf_pawns,UBMP8* bf_pawns
						 ) {
	
	int defence,hopen,f,r;
	int w_ksq = plist[wking]->sq;
    int b_ksq = plist[bking]->sq;
	UBMP8 all_pawn_f = pawnrec.w_pawn_f | pawnrec.b_pawn_f;
    
	pawnrec.w_ksq = w_ksq;
	pawnrec.b_ksq = b_ksq;

	/*
	black attack
	*/
	if(eval_b_attack) {
		
		pawnrec.b_evaled = 1;
		pawnrec.b_s_attack = 0;
		
		defence = 0;
		hopen = 0;
		f = file(w_ksq);
		r = rank(w_ksq);
		
		BITBOARD bb = (((pawns_bb[black] & ~file_mask[FILEH]) >> 7) |
			           ((pawns_bb[black] & ~file_mask[FILEA]) >> 9) ) &
					   king_attacks(SQ8864(w_ksq));
		pawnrec.b_attack = popcnt_sparse(bb);


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
		pawnrec.b_s_attack -= (10 * defence); 
		
		/*pawn storm on white king*/
		if(abs(file(w_ksq) - file(b_ksq)) > 2)  {
			register int r1,r2,r3;
			if(((r1 = first_bit[bf_pawns[f]]) == 8) || (r1 <= r + 1)) r1 = RANK8;
			if(((r2 = (f == FILEA ? 8 : first_bit[bf_pawns[f - 1]])) == 8) || (r2 <= r + 1)) r2 = RANK8;
			if(((r3 = (f == FILEH ? 8 : first_bit[bf_pawns[f + 1]])) == 8) || (r3 <= r + 1)) r3 = RANK8;
			pawnrec.b_s_attack += (21 - (r1 + r2 + r3)) * 8;
		}
		
		/*open files around king*/
		if(!(all_pawn_f & mask[f])) pawnrec.b_s_attack += 20;
		if(f > FILEA && !(all_pawn_f & mask[f - 1])) pawnrec.b_s_attack += 10;
		if(f < FILEH && !(all_pawn_f & mask[f + 1])) pawnrec.b_s_attack += 10;
		
		/*penalize king sitting on half open files,and centre squares*/
		pawnrec.b_attack += (king_on_file[f] +
				        king_on_rank[r] +
						king_on_hopen[hopen]);
	}
    /*
	white attack
    */
	if(eval_w_attack) {
		
		pawnrec.w_evaled = 1;
		pawnrec.w_s_attack = 0;
		defence = 0;
		hopen = 0;
		f = file(b_ksq);
		r = rank(b_ksq);
		
		BITBOARD bb = (((pawns_bb[white] & ~file_mask[FILEA]) << 7) |
			           ((pawns_bb[white] & ~file_mask[FILEH]) << 9) ) &
					   king_attacks(SQ8864(b_ksq));
		pawnrec.w_attack = popcnt_sparse(bb);
		
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
		pawnrec.w_s_attack -= (10 * defence);
		
		/*pawn storm on black king*/
		if(abs(file(w_ksq) - file(b_ksq)) > 2)  {
			register int r1,r2,r3;
			if(((r1 = last_bit[wf_pawns[f]]) == 8) || (r1 >= r - 1)) r1 = RANK1;
			if(((r2 = (f == FILEA ? 8 : last_bit[wf_pawns[f - 1]])) == 8) || (r2 >= r - 1)) r2 = RANK1;
			if(((r3 = (f == FILEH ? 8 : last_bit[wf_pawns[f + 1]])) == 8) || (r3 >= r - 1)) r3 = RANK1;
			pawnrec.w_s_attack += (r1 + r2 + r3) * 8;
		}
		
		/*open files around king*/
		if(!(all_pawn_f & mask[f])) pawnrec.w_s_attack += 20;
		if(f > FILEA && !(all_pawn_f & mask[f - 1])) pawnrec.w_s_attack += 10;
		if(f < FILEH && !(all_pawn_f & mask[f + 1])) pawnrec.w_s_attack += 10;
		
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

static const SCORE DUO(2,4);
static const SCORE DOUBLED(8,10);
static const SCORE ISOLATED_ON_OPEN(15,20);
static const SCORE ISOLATED_ON_CLOSED(10,10);
static const SCORE WEAK_ON_OPEN(12,8);
static const SCORE WEAK_ON_CLOSED(6,6);


SCORE SEARCHER::eval_pawns(int eval_w_attack,int eval_b_attack,
						 UBMP8* wf_pawns,UBMP8* bf_pawns) {
	register SCORE score;
	register PLIST pawnl;

	if(probe_pawn_hash(pawn_hash_key,score,pawnrec)) {
		/*evaluate pawn cover*/
		if(eval_w_attack || eval_b_attack) {
			if(    pawnrec.w_ksq == plist[wking]->sq 
				&& pawnrec.b_ksq == plist[bking]->sq
				&& pawnrec.w_evaled >= eval_w_attack
				&& pawnrec.b_evaled >= eval_b_attack );
			else  {
				eval_pawn_cover(eval_w_attack,eval_b_attack,wf_pawns,bf_pawns);
			}
		}
	} else {
		register int sq,tsq,f,r;
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
				score.sub(DOUBLED);
			}
			/*duo/weak/isolated pawns*/
			if(wp_attacks(sq + UU)) {
				score.add(DUO);
			} else if((f == FILEA || !wf_pawns[f - 1]) &&
				      (f == FILEH || !wf_pawns[f + 1]) ) {
				if(!(up_mask[r] & (bf_pawns[f] | wf_pawns[f]))) 
					score.sub(ISOLATED_ON_OPEN);
				else 
					score.sub(ISOLATED_ON_CLOSED);
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
						score.sub(WEAK_ON_OPEN);
					else
						score.sub(WEAK_ON_CLOSED);
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
					score.add(passed_bonus[r] / 3,passed_bonus[r] / 2);
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
				score.add(DOUBLED);
			}
			/*duo/weak/isolated pawns*/
			if(bp_attacks(sq + DD)) {
				score.sub(DUO);
			} else if((f == FILEA || !bf_pawns[f - 1]) &&
				      (f == FILEH || !bf_pawns[f + 1]) ) {
				if(!(down_mask[r] & (bf_pawns[f] | wf_pawns[f])))
					score.add(ISOLATED_ON_OPEN);
				else
					score.add(ISOLATED_ON_CLOSED);
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
						score.add(WEAK_ON_OPEN);
					else
						score.add(WEAK_ON_CLOSED);
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
					score.sub(passed_bonus[7 - r] / 3,passed_bonus[7 - r] / 2);
			}
			/*end*/
			pawnl = pawnl->next;
		}
		
        /*evaluate pawn cover*/
		if(eval_w_attack || eval_b_attack) {
			eval_pawn_cover(eval_w_attack,eval_b_attack,wf_pawns,bf_pawns);
		}
		
		/*scale*/
		score.mid = (score.mid * PAWN_STRUCT_MG) / 16;
		score.end = (score.end * PAWN_STRUCT_EG) / 16;

        /*store in hash table*/
		record_pawn_hash(pawn_hash_key,score,pawnrec);
	}
	
	return score;
}

/*
passed pawn evaluation
*/

int SEARCHER::eval_passed_pawns(UBMP8* wf_pawns,UBMP8* bf_pawns,UBMP8& all_pawn_f) {
	register UBMP8 passed;
	register int sq,f,r;
	int w_score,b_score,passed_score,rank_score;
	int qdist,w_best_qdist = RANK8,b_best_qdist = RANK8;
	int w_ksq = plist[wking]->sq;
    int b_ksq = plist[bking]->sq;

	w_score = 0;
	b_score = 0;
	
	passed = pawnrec.w_passed;
	while(passed) {
		f = first_bit[passed];
		r = last_bit[wf_pawns[f]];
		sq = SQ(r,f);

		passed_score = rank_score = passed_bonus[r];

		/*Support/Attack*/
		if(wp_attacks(sq + UU))
			passed_score += rank_score;

		if(board[sq + UU] != blank)
			passed_score -= rank_score / 2;

		if(wp_attacks(sq))
			passed_score += rank_score / 4;
        
		if(piece_c[black] < 9) {
			passed_score += (rank_score * f_distance(b_ksq,sq)) / 8;
		    passed_score += (rank_score * (9 - distance(w_ksq,sq))) / 12;
		} else {
			passed_score += (rank_score * f_distance(b_ksq,sq)) / 12;
            passed_score += (rank_score * (9 - distance(w_ksq,sq))) / 16;
		}
		
		/*opponent has no pieces*/
        if(piece_c[black] == 0) {
		    qdist = RANK8 - r;
            if(player == black) qdist++;
			for(int tsq = sq + UU;tsq <= A8 + f;tsq += UU) {
				if(board[tsq] != blank) qdist++;
			}
			if(r == RANK2 && board[sq + UU] == blank && board[sq + UUU] == blank) 
				qdist--;
			if(qdist < distance(b_ksq,A8 + f)) {
				w_best_qdist = min(qdist,w_best_qdist);
			}
			pstack->evalrec.indicator |= AVOID_LAZY;
		}
        /*end*/
		w_score += passed_score;

		passed ^= mask[f];
	}
	passed = pawnrec.b_passed;
	while(passed) {
        f = first_bit[passed];
		r = first_bit[bf_pawns[f]];
		sq = SQ(r,f);

        passed_score = rank_score = passed_bonus[7 - r];

		/*Support/Attack*/
		if(bp_attacks(sq + DD))
			passed_score += rank_score;
		
		if(board[sq + DD] != blank)
			passed_score -= rank_score / 2;

		if(bp_attacks(sq))
			passed_score += rank_score / 4;
        
		if(piece_c[white] < 9) {
			passed_score += (rank_score * f_distance(w_ksq,sq)) / 8;
		    passed_score += (rank_score * (9 - distance(b_ksq,sq))) / 12;
		} else {
			passed_score += (rank_score * f_distance(w_ksq,sq)) / 12;
            passed_score += (rank_score * (9 - distance(b_ksq,sq))) / 16;
		}
			
		/*opponent has no pieces*/
        if(piece_c[white] == 0) {
		    qdist = r;
            if(player == white) qdist++;
			for(int tsq = sq + DD;tsq >= A1 + f;tsq += DD) {
				if(board[tsq] != blank) qdist++;
			}
			if(r == RANK7 && board[sq + DD] == blank && board[sq + DDD] == blank) 
				qdist--;
			if(qdist < distance(w_ksq,A1 + f)) {
				b_best_qdist = min(qdist,b_best_qdist);
			}
			pstack->evalrec.indicator |= AVOID_LAZY;
		}
		/*end*/
	 	b_score += passed_score;

		passed ^= mask[f];
    }

    /*unstoppable passer*/
	if(w_best_qdist < b_best_qdist) {
		w_score += 600;
	} else if(b_best_qdist < w_best_qdist) {
		b_score += 600;
	}
    /*king close to pawn center*/
	if(!piece_c[white] || !piece_c[black]) {
		int wclos = (7 - abs(center_bit[all_pawn_f] - file(w_ksq)));
		int bclos = (7 - abs(center_bit[all_pawn_f] - file(b_ksq)));
		if(wclos > bclos) w_score += king_to_pawns[wclos - bclos];
		else b_score += king_to_pawns[bclos - wclos];
	}
	/*defender is knight*/
	int strech,pawns;
	if((pawns = pawnrec.w_pawn_f) != 0 && piece_c[black] == 3 && man_c[bknight] == 1) {
        strech = last_bit[pawns] - first_bit[pawns];
		if(pawnrec.w_passed) {
			if(strech > 2) w_score += 50;
			else w_score += 25;
		} else {
			if(strech > 2) w_score += 25;
			else w_score += 12;
		}
	}
	if((pawns = pawnrec.b_pawn_f) != 0 && piece_c[white] == 3 && man_c[wknight] == 1) {
        strech = last_bit[pawns] - first_bit[pawns];
		if(pawnrec.b_passed) {
			if(strech > 2) b_score += 50;
			else b_score += 25;
		} else {
			if(strech > 2) b_score += 25;
			else b_score += 12;
		}
	}
	/*end*/
	return (w_score - b_score);
}
/*
material score
*/
void SEARCHER::eval_win_chance(SCORE& w_score,SCORE& b_score,int& w_win_chance,int& b_win_chance) {
	register int w_ksq = plist[wking]->sq;
    register int b_ksq = plist[bking]->sq;
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
	int temp,temp1;

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

	if(temp >= 5) w_score.add(4 * EXCHANGE_BONUS);                               //[R,Q] vs Ps
	else if(temp <= -5) b_score.add(4 * EXCHANGE_BONUS);
	else if(temp >= 3) w_score.add(2 * EXCHANGE_BONUS,3 * EXCHANGE_BONUS / 2);   //M vs 3P
	else if(temp <= -3) b_score.add(2 * EXCHANGE_BONUS,3 * EXCHANGE_BONUS / 2);
	else if(temp1 >= 3) w_score.add(EXCHANGE_BONUS);                             //3M vs (2R or Q)
	else if(temp1 <= -3) b_score.add(EXCHANGE_BONUS);
	else if(temp1 == 2) w_score.add(EXCHANGE_BONUS);                             //2M vs R
	else if(temp1 == -2) b_score.add(EXCHANGE_BONUS);
	else if(temp == 2) w_score.add(EXCHANGE_BONUS);                              //R vs M
	else if(temp == -2) b_score.add(EXCHANGE_BONUS);

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
		if(w_pawn_c > b_pawn_c) w_score.adde(100);
		else if(b_pawn_c > w_pawn_c) b_score.adde(100);
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
		int psq = A1,pfile = 0,bsq = (w_piece_value_c == 0) ? A1 : plist[wbishop]->sq,ksq = plist[bking]->sq;
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
		int psq = H8,pfile = 0,bsq = (b_piece_value_c == 0) ? A1 : plist[bbishop]->sq,ksq = plist[wking]->sq;
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
				for(int sq = psq + RR;sq > B1;sq += DD) {
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
		&& abs(w_pawn_c - b_pawn_c) <= 2
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
Lazy exit
*/
bool SEARCHER::eval_lazy_exit(int lazy, int lazy_margin,int phase,SCORE& w_score,SCORE& b_score,
							  int w_win_chance,int b_win_chance) {
	/*adjust lazy score*/
	if(player == white) {
		pstack->lazy_score = ((w_score.mid - b_score.mid) * (phase) +
			                   (w_score.end - b_score.end) * (MAX_MATERIAL - phase)) / MAX_MATERIAL;
		if(pstack->lazy_score > 0) {
			pstack->lazy_score = (pstack->lazy_score * w_win_chance) / 8;
		} else {
			pstack->lazy_score = (pstack->lazy_score * b_win_chance) / 8;
		}
	} else {
		pstack->lazy_score = ((b_score.mid - w_score.mid) * (phase) +
			                   (b_score.end - w_score.end) * (MAX_MATERIAL - phase)) / MAX_MATERIAL;
		if(pstack->lazy_score > 0) {
			pstack->lazy_score = (pstack->lazy_score * b_win_chance) / 8;
		} else {
			pstack->lazy_score = (pstack->lazy_score * w_win_chance) / 8;
		}
	}

	/*lazy evaluation - 1*/
	if(lazy
		&& !hstack[hply - 2].checks
		&& !hstack[hply - 1].checks
		&& !((pstack - 1)->evalrec.indicator & AVOID_LAZY)
		) {
		int positional_delta;
		
		/*do forced lazy eval*/
		if(lazy == DO_LAZY) {
			positional_delta = (pstack - 1)->lazy_score - (pstack - 1)->actual_score;
			pstack->actual_score = pstack->lazy_score + positional_delta;
			goto CUT;
		}

		/*lazy eval*/
		if(lazy == TRY_LAZY) {
			int LAZY_MARGIN;
			if(PIECE(m_capture(hstack[hply - 1].move)) == queen)
				LAZY_MARGIN = LAZY_MARGIN_MAX;
			else
				LAZY_MARGIN = lazy_margin;
			
			positional_delta = (pstack - 1)->lazy_score - (pstack - 1)->actual_score;
			pstack->actual_score = pstack->lazy_score + positional_delta;
			
			if(pstack->actual_score - LAZY_MARGIN > pstack->beta ||
			   pstack->actual_score + LAZY_MARGIN < pstack->alpha)
			   goto CUT;
		}
	}

	/*lazy evaluation - 2*/
	if(lazy) {
		pstack->actual_score = pstack->lazy_score;
		if(lazy == DO_LAZY)
			goto CUT;
		if(lazy == TRY_LAZY) {
			if(pstack->actual_score - LAZY_MARGIN_MAX > pstack->beta ||
			   pstack->actual_score + LAZY_MARGIN_MAX < pstack->alpha)
			   goto CUT;
		}
	}

	/*failure*/
	return false;
    
CUT:
	/*success*/
	lazy_evals++;
	pstack->evalrec.indicator |= AVOID_LAZY;
	return true;
}
/*
* Modify eval parameters
*/
#ifdef TUNE
bool check_eval_params(char** commands,char* command,int& command_num) {
	if(!strcmp(command, "LAZY_MARGIN_MIN")) {
		LAZY_MARGIN_MIN = atoi(commands[command_num++]);
	} else if(!strcmp(command, "LAZY_MARGIN_MAX")) {
		LAZY_MARGIN_MAX = atoi(commands[command_num++]);
	} else if(!strcmp(command, "KNIGHT_OUTPOST")) {
		KNIGHT_OUTPOST = atoi(commands[command_num++]);
	} else if(!strcmp(command, "BISHOP_OUTPOST")) {
		BISHOP_OUTPOST = atoi(commands[command_num++]);
	} else if(!strcmp(command, "KNIGHT_MOB")) {
		KNIGHT_MOB = atoi(commands[command_num++]);
	} else if(!strcmp(command, "BISHOP_MOB")) {
		BISHOP_MOB = atoi(commands[command_num++]);
	} else if(!strcmp(command, "ROOK_MOB")) {
		ROOK_MOB = atoi(commands[command_num++]);
	} else if(!strcmp(command, "QUEEN_MOB")) {
		QUEEN_MOB = atoi(commands[command_num++]);
	} else if(!strcmp(command, "PASSER_MG")) {
		PASSER_MG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "PASSER_EG")) {
		PASSER_EG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "PAWN_STRUCT_MG")) {
		PAWN_STRUCT_MG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "PAWN_STRUCT_EG")) {
		PAWN_STRUCT_EG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "ROOK_ON_7TH")) {
		ROOK_ON_7TH = atoi(commands[command_num++]);
	} else if(!strcmp(command, "ROOK_ON_OPEN")) {
		ROOK_ON_OPEN = atoi(commands[command_num++]);
	} else if(!strcmp(command, "QUEEN_MG")) {
		QUEEN_MG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "QUEEN_EG")) {
		QUEEN_EG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "ROOK_MG")) {
		ROOK_MG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "ROOK_EG")) {
		ROOK_EG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "BISHOP_MG")) {
		BISHOP_MG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "BISHOP_EG")) {
		BISHOP_EG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "KNIGHT_MG")) {
		KNIGHT_MG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "KNIGHT_EG")) {
		KNIGHT_EG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "PAWN_MG")) {
		PAWN_MG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "PAWN_EG")) {
		PAWN_EG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "BISHOP_PAIR_MG")) {
		BISHOP_PAIR_MG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "BISHOP_PAIR_EG")) {
		BISHOP_PAIR_EG = atoi(commands[command_num++]);
	} else if(!strcmp(command, "EXCHANGE_BONUS")) {
		EXCHANGE_BONUS = atoi(commands[command_num++]);
	} else if(!strcmp(command, "ATTACK_WEIGHT")) {
		ATTACK_WEIGHT = atoi(commands[command_num++]);
	} else if(!strcmp(command, "TROPISM_WEIGHT")) {
		TROPISM_WEIGHT = atoi(commands[command_num++]);
	} else if(!strcmp(command, "PAWN_GUARD")) {
		PAWN_GUARD = atoi(commands[command_num++]);
	} else {
		return false;
	}
	return true;
}
void print_eval_params() {
	print("feature option=\"LAZY_MARGIN_MIN -spin 150 0 2000\"\n");
	print("feature option=\"LAZY_MARGIN_MAX -spin 300 0 2000\"\n");
	print("feature option=\"KNIGHT_OUTPOST -spin 16 0 64\"\n");
    print("feature option=\"BISHOP_OUTPOST -spin 12 0 64\"\n");
	print("feature option=\"KNIGHT_MOB -spin 16 0 64\"\n");
	print("feature option=\"BISHOP_MOB -spin 16 0 64\"\n");
	print("feature option=\"ROOK_MOB -spin 16 0 64\"\n");
	print("feature option=\"QUEEN_MOB -spin 16 0 64\"\n");
	print("feature option=\"PASSER_MG -spin 12 0 64\"\n");
	print("feature option=\"PASSER_EG -spin 16 0 64\"\n");
	print("feature option=\"PAWN_STRUCT_MG -spin 12 0 64\"\n");
	print("feature option=\"PAWN_STRUCT_EG -spin 16 0 64\"\n");
	print("feature option=\"ROOK_ON_7TH -spin 12 0 64\"\n");
	print("feature option=\"ROOK_ON_OPEN -spin 16 0 64\"\n");
	print("feature option=\"QUEEN_MG -spin 1050 0 2000\"\n");
	print("feature option=\"QUEEN_EG -spin 1050 0 2000\"\n");
	print("feature option=\"ROOK_MG -spin 500 0 2000\"\n");
	print("feature option=\"ROOK_EG -spin 500 0 2000\"\n");
	print("feature option=\"BISHOP_MG -spin 325 0 2000\"\n");
	print("feature option=\"BISHOP_EG -spin 325 0 2000\"\n");
	print("feature option=\"KNIGHT_MG -spin 325 0 2000\"\n");
	print("feature option=\"KNIGHT_EG -spin 325 0 2000\"\n");
	print("feature option=\"PAWN_MG -spin 80 0 2000\"\n");
	print("feature option=\"PAWN_EG -spin 100 0 2000\"\n");
	print("feature option=\"BISHOP_PAIR_MG -spin 16 0 64\"\n");
	print("feature option=\"BISHOP_PAIR_EG -spin 16 0 64\"\n");
	print("feature option=\"EXCHANGE_BONUS -spin 45 0 200\"\n");
	print("feature option=\"PAWN_GUARD -spin 16 0 64\"\n");
	print("feature option=\"ATTACK_WEIGHT -spin 16 0 64\"\n");
	print("feature option=\"TROPISM_WEIGHT -spin 8 0 64\"\n");
}

#endif
