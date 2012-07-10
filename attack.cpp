#include "scorpio.h"

static const UBMP8 t_sqatt_pieces[] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0, 10,  0,  0,  0,  0,  0,  0,
  6,  0,  0,  0,  0,  0,  0, 10,  0,  0, 10,  0,  0,  0,  0,  0,
  6,  0,  0,  0,  0,  0, 10,  0,  0,  0,  0, 10,  0,  0,  0,  0,
  6,  0,  0,  0,  0, 10,  0,  0,  0,  0,  0,  0, 10,  0,  0,  0,
  6,  0,  0,  0, 10,  0,  0,  0,  0,  0,  0,  0,  0, 10,  0,  0,
  6,  0,  0, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 10, 16,
  6, 16, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 16, 75,
  7, 75, 16,  0,  0,  0,  0,  0,  0,  6,  6,  6,  6,  6,  6,  7,
  0,  7,  6,  6,  6,  6,  6,  6,  0,  0,  0,  0,  0,  0, 16, 43,
  7, 43, 16,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 10, 16,
  6, 16, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 10,  0,  0,
  6,  0,  0, 10,  0,  0,  0,  0,  0,  0,  0,  0, 10,  0,  0,  0,
  6,  0,  0,  0, 10,  0,  0,  0,  0,  0,  0, 10,  0,  0,  0,  0,
  6,  0,  0,  0,  0, 10,  0,  0,  0,  0, 10,  0,  0,  0,  0,  0,
  6,  0,  0,  0,  0,  0, 10,  0,  0, 10,  0,  0,  0,  0,  0,  0,
  6,  0,  0,  0,  0,  0,  0, 10,  0,  0,  0,  0,  0,  0,  0,  0
};

static const BMP8 t_sqatt_step[] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,-17,  0,  0,  0,  0,  0,  0,
-16,  0,  0,  0,  0,  0,  0,-15,  0,  0,-17,  0,  0,  0,  0,  0,
-16,  0,  0,  0,  0,  0,-15,  0,  0,  0,  0,-17,  0,  0,  0,  0,
-16,  0,  0,  0,  0,-15,  0,  0,  0,  0,  0,  0,-17,  0,  0,  0,
-16,  0,  0,  0,-15,  0,  0,  0,  0,  0,  0,  0,  0,-17,  0,  0,
-16,  0,  0,-15,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,-17,  0,
-16,  0,-15,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,-17,
-16,-15,  0,  0,  0,  0,  0,  0,  0, -1, -1, -1, -1, -1, -1, -1,
  0,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0, 15,
 16, 17,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15,  0,
 16,  0, 17,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15,  0,  0,
 16,  0,  0, 17,  0,  0,  0,  0,  0,  0,  0,  0, 15,  0,  0,  0,
 16,  0,  0,  0, 17,  0,  0,  0,  0,  0,  0, 15,  0,  0,  0,  0,
 16,  0,  0,  0,  0, 17,  0,  0,  0,  0, 15,  0,  0,  0,  0,  0,
 16,  0,  0,  0,  0,  0, 17,  0,  0, 15,  0,  0,  0,  0,  0,  0,
 16,  0,  0,  0,  0,  0,  0, 17,  0,  0,  0,  0,  0,  0,  0,  0
};

const UBMP8* const _sqatt_pieces = t_sqatt_pieces + 0x80;
const BMP8* const _sqatt_step = t_sqatt_step + 0x80;

/*is pinned on king*/ 
int SEARCHER::pinned_on_king(int sq,int col) const {
	register int sq1;
	register int king_sq = plist[COMBINE(col,king)]->sq;
	register int step = sqatt_step(sq - king_sq);

	if(step && !blocked(sq,king_sq)) {
		for(sq1 = sq + step;board[sq1] == blank;sq1 += step);
		if(!(sq1 & 0x88)) {
			if(PCOLOR(board[sq1]) == invert(col) 
				&& (sqatt_pieces(sq1 - king_sq) & piece_mask[board[sq1]]))
				return step;
		}
	}
	return 0; 
}

/*is square attacked by color?*/
int SEARCHER::attacks(int col,int sq) const {
    register PLIST current;
	
	if(col == white) {
		/*pawn*/
		if(board[sq + LD] == wpawn) return true;
        if(board[sq + RD] == wpawn) return true;
		/*knight*/
		current = plist[wknight];
		while(current) {
			if(sqatt_pieces(sq - current->sq) & NM)
				return true;
			current = current->next;
		}
		/*bishop*/
		current = plist[wbishop];
		while(current) {
			if(sqatt_pieces(sq - current->sq) & BM)
				if(!blocked(current->sq,sq))
					return true;
			current = current->next;
		}
		/*rook*/
		current = plist[wrook];
		while(current) {
			if(sqatt_pieces(sq - current->sq) & RM)
				if(!blocked(current->sq,sq))
					return true;
			current = current->next;
		}
		/*queen*/
		current = plist[wqueen];
		while(current) {
			if(sqatt_pieces(sq - current->sq) & QM)
				if(!blocked(current->sq,sq))
					return true;
			current = current->next;
		}
		/*king*/
		if(sqatt_pieces(sq - plist[wking]->sq) & KM)
			return true;
	} else if(col == black) {
		/*pawn*/
		if(board[sq + RU] == bpawn) return true;
        if(board[sq + LU] == bpawn) return true;
		/*knight*/
		current = plist[bknight];
		while(current) {
			if(sqatt_pieces(sq - current->sq) & NM)
				return true;
			current = current->next;
		}
		/*bishop*/
		current = plist[bbishop];
		while(current) {
			if(sqatt_pieces(sq - current->sq) & BM)
				if(!blocked(current->sq,sq))
					return true;
			current = current->next;
		}
		/*rook*/
		current = plist[brook];
		while(current) {
			if(sqatt_pieces(sq - current->sq) & RM)
				if(!blocked(current->sq,sq))
					return true;
			current = current->next;
		}
		/*queen*/
		current = plist[bqueen];
		while(current) {
			if(sqatt_pieces(sq - current->sq) & QM)
				if(!blocked(current->sq,sq))
					return true;
			current = current->next;
		}
		/*king*/
		if(sqatt_pieces(sq - plist[bking]->sq) & KM)
			return true;
	}
	return false;
}
/*
 Faster checks and in_check. These are called before move is made
 NB: They don't work at hply = 0.
*/
int SEARCHER::checks(MOVE move,int& rev_check) const {
	register int from = m_from(move),to = m_to(move),sq,step,mvstep;
	int pic = board[from];
	int ksq = plist[COMBINE(opponent,king)]->sq;
	int special,check = 0;
	
	rev_check = 0;
	
	if(m_promote(move))
		pic = m_promote(move);
	
	/*direct check*/
	if (pic == COMBINE(player,pawn)) {
		if(ksq - to == (pawn_dir[player] + RR) || ksq - to == (pawn_dir[player] + LL))
			check = 1;
	} else if(sqatt_pieces(ksq - to) & piece_mask[pic]) {
		if (pic == COMBINE(player,king) || pic == COMBINE(player,knight))
			check = 1;
		else {
			special = (m_promote(move) && (sqatt_step(to - from) == sqatt_step(to - ksq)));
			if(special) {
				if(blocked(ksq,from) == 0)
					check = 1;
			} else {
				if(blocked(ksq,to) == 0)
					check = 1;
			}
		}
	}
	/*revealed check*/
	step = sqatt_step(from - ksq);
	mvstep = sqatt_step(to - from);
	if(step && ABS(step) != ABS(mvstep)) {
		if(blocked(ksq,from) == 0) {
			sq = from + step;
			while(board[sq] == blank) sq += step;
			if(!(sq & 0x88)) {
				if(PCOLOR(board[sq]) == player 
					&& (sqatt_pieces(sq - ksq) & piece_mask[board[sq]])) {
					check += 2;
					rev_check = sq;
				}
			}
		}
	}
	
	/*castling*/
	if(is_castle(move)) {
		int cast = ((to > from) ? (to + LL):(to + RR));
		if(sqatt_pieces(cast - ksq) & piece_mask[COMBINE(player,rook)]) {
			special = (rank(cast) == rank(ksq));
			if(special) {
				if(!blocked(from,ksq)) {
					check += 2;
					rev_check = cast;
				}
			}else{
				if(!blocked(cast,ksq)) {
					check += 2;
					rev_check = cast;
				}
			}
		}
	/*enpassant*/
	} else if(is_ep(move)) {
		int epsq = to + ((to > from) ? DD:UU);
		step = sqatt_step(epsq - ksq);
		if(step && ABS(step) != UU) {
			int nsq,fsq;
			if(ABS(step) == RR){
				if(distance(from,ksq) < distance(epsq,ksq)) {
					nsq = from;
					fsq = epsq;
				} else {
					nsq = epsq;
					fsq = from;
				}
			} else {
				nsq = epsq;
				fsq = epsq;
			}
			if(!blocked(nsq,ksq)){
				sq = fsq + step;
				while(board[sq] == blank) {sq += step;}
				if(!(sq & 0x88)) {
					if(PCOLOR(board[sq]) == player 
						&& (sqatt_pieces(sq - ksq) & piece_mask[board[sq]])) {
						check += 2;
						rev_check = sq;
					}
				}
			}
		}
	}
	
	return check;
}

int SEARCHER::in_check(MOVE move) const {
	register int from = m_from(move),to = m_to(move),sq,step,mvstep;
	register int ksq = plist[COMBINE(player,king)]->sq;

	/*castling*/
	if(is_castle(move)) {
		return false;
	}

	/*other king moves*/
	if(PIECE(m_piece(move)) == king) {
		return attacks(opponent,to);
	}

	/*revealed check*/
	step = sqatt_step(from - ksq);
	mvstep = sqatt_step(to - from);
	if(step && ABS(step) != ABS(mvstep)) {
		if(!blocked(ksq,from)) {
			sq = from + step;
			while(board[sq] == blank) sq += step;
			if(!(sq & 0x88)) {
				if(PCOLOR(board[sq]) == opponent 
					&& (sqatt_pieces(sq - ksq) & piece_mask[board[sq]])) {
					return true;
				}
			}
		}
	}

	/*enpassant move*/
	if(is_ep(move)) {
		int epsq = to + ((to > from) ? DD:UU);
		step = sqatt_step(epsq - ksq);
		if(step && ABS(step) != UU) {
			int nsq,fsq;
			if(ABS(step) == RR){
				if(distance(from,ksq) < distance(epsq,ksq)) {
					nsq = from;
					fsq = epsq;
				} else {
					nsq = epsq;
					fsq = from;
				}
			} else {
				nsq = epsq;
				fsq = epsq;
			}
			if(!blocked(nsq,ksq)){
				sq = fsq + step;
				while(board[sq] == blank) {sq += step;}
				if(!(sq & 0x88)) {
					if(PCOLOR(board[sq]) == opponent 
						&& (sqatt_pieces(sq - ksq) & piece_mask[board[sq]])) {
						return true;
					}
				}
			}
		}
	}

	return false;
}

/*used to check legality of move during search*/
int SEARCHER::is_legal_fast(MOVE move) const {
	
	register int from = m_from(move), to = m_to(move),
		         pic = m_piece(move),
                 cap = m_capture(move),
				 prom = m_promote(move),
				 sq;

	if((from & 0x88) || (to & 0x88) || (from == to))
		return false;
	
	if(pic == blank || pic >= elephant)
		return false;

	if(cap >= elephant)
		return false;

	if(prom >= elephant)
		return false;

	if(player != PCOLOR(pic))
		return false;
	
	if(board[from] != pic)
		return false;
	
	if(cap) {
		if(PIECE(pic) == pawn) {
			if(to != from + pawn_dir[player] + RR &&
				to != from + pawn_dir[player] + LL)
				return false;
		}
		if(is_ep(move)) {
			sq = to - pawn_dir[player];
			if(epsquare == to && board[to] == blank && board[sq] == COMBINE(opponent,pawn)) 
				return true;
		} else {
			if(board[to] == cap) {
				if(PIECE(pic) != pawn) {
					if(!(sqatt_pieces(to - from) & piece_mask[pic]))
						return false;
				}
				if(piece_mask[pic] & QRBM) {
					if(!blocked(from,to))
						return true;
				}else
					return true;
			}
		}
	}  else {
		if(board[to] != blank)
			return false;
		if(PIECE(pic) == pawn) {
			if(to == from + pawn_dir[player]) {
				if(board[from + pawn_dir[player]] == blank)
					return true;
			} else if(to == from + 2 * pawn_dir[player]) {
				if(board[from + pawn_dir[player]] == blank &&
					board[from + 2 * pawn_dir[player]] == blank)
					return true;
			} else
				return false;
		} else if(is_castle(move)) {
			if(player == white) {
				if(!attacks(black,E1)) {
					if(to == G1 &&
						castle & WSC_FLAG &&
						board[F1] == blank &&
						board[G1] == blank &&
						!attacks(black,F1) &&
						!attacks(black,G1))
						return true;
					if(to == C1 &&
						castle & WLC_FLAG &&
						board[B1] == blank &&
						board[C1] == blank &&
						board[D1] == blank &&
						!attacks(black,C1) &&
						!attacks(black,D1)) {
						return true;
					}
				}
			} else if(player == black) {
				if(!attacks(white,E8)) {
					if(to == G8 &&
						castle & BSC_FLAG &&
						board[F8] == blank &&
						board[G8] == blank &&
						!attacks(white,F8) &&
						!attacks(white,G8))
						return true;
					if(to == C8 &&
						castle & BLC_FLAG &&
						board[B8] == blank &&
						board[C8] == blank &&
						board[D8] == blank &&
						!attacks(white,C8) &&
						!attacks(white,D8)) {
						return true;
					}
				}
			}
		} else {
			if(!(sqatt_pieces(to - from) & piece_mask[pic]))
				return false;
			if(piece_mask[pic] & QRBM) {
				if(!blocked(from,to))
					return true;
			} else
				return true;
		}
	}
	return false;
}
