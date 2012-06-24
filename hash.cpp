#include "scorpio.h"

PHASH PROCESSOR::white_hash_tab = 0;
PHASH PROCESSOR::black_hash_tab = 0;
UBMP32 PROCESSOR::hash_tab_mask = (1 << 8) - 1;
UBMP32 PROCESSOR::eval_hash_tab_mask = (1 << 8) - 1;
UBMP32 PROCESSOR::pawn_hash_tab_mask = (1 << 8) - 1;
int PROCESSOR::age;

/*
Allocate tables 
	-Main hasht table is shared.
	-The rest is allocated for each thread.
*/
void PROCESSOR::reset_hash_tab(int id,UBMP32 size) {
	if(size) hash_tab_mask = size - 1;
	else size = hash_tab_mask + 1;
	aligned_reserve<HASH>(white_hash_tab,size);
	aligned_reserve<HASH>(black_hash_tab,size);
}

void PROCESSOR::reset_pawn_hash_tab(UBMP32 size) {
    if(size) pawn_hash_tab_mask = size - 1;
	else size = pawn_hash_tab_mask + 1;
	aligned_reserve<PAWNHASH>(pawn_hash_tab,size);
}

void PROCESSOR::reset_eval_hash_tab(UBMP32 size) {
	if(size) eval_hash_tab_mask = size - 1;
	else size = eval_hash_tab_mask + 1;
	aligned_reserve<EVALHASH>(eval_hash_tab,size);
}

void PROCESSOR::clear_hash_tables() {
	PPROCESSOR proc;
	memset(white_hash_tab,0,(hash_tab_mask + 1) * sizeof(HASH));
	memset(black_hash_tab,0,(hash_tab_mask + 1) * sizeof(HASH));
	for(int i = 0;i < n_processors;i++) {
		proc = processors[i];
		memset(proc->pawn_hash_tab,0,(pawn_hash_tab_mask + 1) * sizeof(PAWNHASH));
		memset(proc->eval_hash_tab,0,(eval_hash_tab_mask + 1) * sizeof(EVALHASH));
	}
}
/*
Main hash table
*/
void PROCESSOR::record_hash(
				 int col,const HASHKEY& hash_key,int depth,int ply,
				 int flags,int score,MOVE move,int mate_threat
				 ) {
	register UBMP32 key = UBMP32(hash_key & hash_tab_mask);
	register PHASHREC pslot,pr_slot = 0;
	int sc,max_sc = MAX_NUMBER;

	if(col == white) pslot = PHASHREC(white_hash_tab + key);
	else pslot = PHASHREC(black_hash_tab + key);

	for(int i = 0; i < 4; i++) {
		if(!pslot->hash_key || pslot->hash_key == hash_key) {
			if(flags == CRAP && pslot->move == move)
				return;

			if(score > WIN_SCORE) 
				score += WIN_PLY * (ply + 1);
			else if(score < -WIN_SCORE) 
				score -= WIN_PLY * (ply + 1);
			
			if(move) 
				pslot->move = UBMP32(move);
			pslot->score = BMP16(score);
			pslot->depth = UBMP8(depth);
			if(mate_threat || (pslot->flags & 4)) 
				pslot->flags = UBMP8((flags - EXACT) | (age << 3) | (4));
			else 
				pslot->flags = UBMP8((flags - EXACT) | (age << 3));
			pslot->hash_key = hash_key;
			return;
		} else {
			sc = (pslot->flags & ~7)                     //8 * age   {larger weight}
				+  DEPTH(pslot->depth)                   //depth     {non-fractional depth}
				+ ((pslot->flags & 3) ? 0 : (4 << 3));   //8 * 4     {EXACT score goes 4 ages up}
			if(sc < max_sc) {
				pr_slot = pslot;
				max_sc = sc;
			}
		}
		pslot++;
	}

	pr_slot->move = UBMP32(move);
	pr_slot->score = BMP16(score);
	pr_slot->depth = UBMP8(depth);
	pr_slot->flags = UBMP8((flags - EXACT) | (mate_threat << 2) | (age << 3));
	pr_slot->hash_key = hash_key;
}

int PROCESSOR::probe_hash(
			   int col,const HASHKEY& hash_key,int depth,int ply,int& score,
			   MOVE& move,int alpha,int beta,int& mate_threat,int& h_depth
			   ) {
	register UBMP32 key = UBMP32(hash_key & hash_tab_mask);
	register PHASHREC pslot;
    register int flags;
	
	if(col == white) pslot = PHASHREC(white_hash_tab + key);
	else pslot = PHASHREC(black_hash_tab + key);
	
	for(int i = 0; i < 4; i++) {
		
		if(pslot->hash_key == hash_key) {
			
			score = pslot->score;
			if(score > WIN_SCORE) 
				score -= WIN_PLY * (ply + 1);
			else if(score < -WIN_SCORE) 
				score += WIN_PLY * (ply + 1);
			
			move = pslot->move;
			mate_threat |= ((pslot->flags >> 2) & 1);
			flags = (pslot->flags & 3) + EXACT;
			h_depth = pslot->depth;
			
			if(h_depth >= depth) {
				if(flags == EXACT) {
					return EXACT;
				} else if(flags == LOWER) {
					if(score >= beta)
						return LOWER;
				} else if(flags == UPPER) {
					if(score <= alpha)
						return UPPER;
				}
			} 

			if(depth - 4 * UNITDEPTH <= h_depth 
				&& (flags == UPPER && score < beta))
				return AVOID_NULL;

			if(depth - 4 * UNITDEPTH <= h_depth
				&& ( (flags == EXACT && score > alpha) 
				  || (flags == LOWER && score >= beta)))
				return HASH_GOOD;

			return HASH_HIT;
		}
		
		pslot++;
	}

	return UNKNOWN;
}
/*
Pawn hash tables
*/
void SEARCHER::record_pawn_hash(const HASHKEY& hash_key,const SCORE& score,const PAWNREC& pawnrec) {
	register PPROCESSOR proc = processors[processor_id];
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::pawn_hash_tab_mask);
	register PPAWNHASH pawn_hash = proc->pawn_hash_tab + key; 
	
	pawn_hash->checksum = hash_key;
	pawn_hash->score = score;
	pawn_hash->pawnrec = pawnrec;
}
int SEARCHER::probe_pawn_hash(const HASHKEY& hash_key,SCORE& score,PAWNREC& pawnrec) {
	register PPROCESSOR proc = processors[processor_id];
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::pawn_hash_tab_mask);
	register PPAWNHASH pawn_hash = proc->pawn_hash_tab + key; 
	
	if(pawn_hash->checksum == hash_key) {
		score = pawn_hash->score;
		pawnrec = pawn_hash->pawnrec;
		return 1;
	}
	return 0;
}
/*
Eval hash tables
*/
void SEARCHER::record_eval_hash(const HASHKEY& hash_key,int score,int lazy_score,const EVALREC& evalrec) {
    register PPROCESSOR proc = processors[processor_id];
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::eval_hash_tab_mask);
	register PEVALHASH eval_hash = proc->eval_hash_tab + key; 
	
	eval_hash->checksum = hash_key;
	eval_hash->score = (BMP16)score;
    eval_hash->lazy_score = (BMP16)lazy_score;
	eval_hash->evalrec = evalrec;
}
int SEARCHER::probe_eval_hash(const HASHKEY& hash_key,int& score,int& lazy_score,EVALREC& evalrec) {
	register PPROCESSOR proc = processors[processor_id];
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::eval_hash_tab_mask);
	register PEVALHASH eval_hash = proc->eval_hash_tab + key; 
	
	if(eval_hash->checksum == hash_key) {
		score = eval_hash->score;
		lazy_score = eval_hash->lazy_score;
		evalrec = eval_hash->evalrec;
		return 1;
	}
	return 0;
}
