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
void PROCESSOR::reset_hash_tab(UBMP32 size) {
	if(size) hash_tab_mask = size - 1;
	else size = hash_tab_mask + 1;
	if(white_hash_tab) delete[] white_hash_tab;
	if(black_hash_tab) delete[] black_hash_tab;
    white_hash_tab = new HASH[size];
	black_hash_tab = new HASH[size];
	memset(white_hash_tab,0,size * sizeof(HASH));
	memset(black_hash_tab,0,size * sizeof(HASH));
}

void PROCESSOR::reset_pawn_hash_tab(UBMP32 size) {
    if(size) pawn_hash_tab_mask = size - 1;
	else size = pawn_hash_tab_mask + 1;
	if(pawn_hash_tab) delete[] pawn_hash_tab;
	pawn_hash_tab = new PAWNHASH[size];
	memset(pawn_hash_tab,0,size * sizeof(PAWNHASH));
}

void PROCESSOR::reset_eval_hash_tab(UBMP32 size) {
	if(size) eval_hash_tab_mask = size - 1;
	else size = eval_hash_tab_mask + 1;
	if(eval_hash_tab) delete[] eval_hash_tab;
	eval_hash_tab = new EVALHASH[size];
	memset(eval_hash_tab,0,size * sizeof(EVALHASH));
}

void PROCESSOR::clear_hash_tables() {
	PPROCESSOR proc;
	memset(white_hash_tab,0,(hash_tab_mask + 1) * sizeof(HASH));
	memset(black_hash_tab,0,(hash_tab_mask + 1) * sizeof(HASH));
	for(int i = 0;i < n_processors;i++) {
		proc = &processors[i];
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
	register PHASHREC pslot,pr_slot;
	int sc,max_sc = MAX_NUMBER;

	if(col == white) pslot = PHASHREC(white_hash_tab + key);
	else pslot = PHASHREC(black_hash_tab + key);

	for(int i = 0; i < 4; i++) {
		if(pslot->hash_key == hash_key) {
			/*fake pv storage*/
			if(flags == CRAP && pslot->move == move)
				return;

			if(score > WIN_SCORE) 
				score += WIN_PLY * (ply + 1);
			else if(score < -WIN_SCORE) 
				score -= WIN_PLY * (ply + 1);
			
			if(move)
				pslot->move = BMP32(move);
			pslot->score = BMP16(score);
			pslot->depth = BMP8(depth);
			pslot->flags = BMP8((flags - EXACT) | (age << 3));
			if(mate_threat)
				pslot->flags |= BMP8((mate_threat << 2));
			pslot->hash_key = hash_key;
			return;
		} else {
			sc = (pslot->flags & AGE_MASK)             //age << 3
				+  pslot->depth                        //depth 
				+ (pslot->flags & 3) ? 0 : (4 << 3);   //exact score 4 ages up!
            if(sc < max_sc) pr_slot = pslot;
		}
		pslot++;
	}

	pr_slot->move = BMP32(move);
	pr_slot->score = BMP16(score);
	pr_slot->depth = BMP8(depth);
	pr_slot->flags = BMP8((flags - EXACT) | (mate_threat << 2) | (age << 3));
	pr_slot->hash_key = hash_key;
}

int PROCESSOR::probe_hash(
			   int col,const HASHKEY& hash_key,int depth,int ply,int& score,
			   MOVE& move,int alpha,int beta,int& mate_threat,int& h_depth
			   ) {
	register UBMP32 key = UBMP32(hash_key & hash_tab_mask);
    register int flags, avd_null = 0, hash_hit = 0;
	register PHASHREC pslot;
	
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
			
			if(pslot->depth >= depth) {
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
			
			if(depth - 3 - 1 <= h_depth 
				&& score < beta 
				&& flags == UPPER)
				avd_null = 1;
			hash_hit = 1;

			break;
		}
		
		pslot++;
	}
	
	if(avd_null)
		return AVOID_NULL;
	else if(hash_hit)
		return HASH_HIT;
	else
		return UNKNOWN;
}
/*
Pawn hash tables
*/
void SEARCHER::record_pawn_hash(const HASHKEY& hash_key,const SCORE& score,const PAWNREC& pawnrec) {
	register PPROCESSOR proc = processors + processor_id;
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::pawn_hash_tab_mask);
	register PPAWNHASH pawn_hash = proc->pawn_hash_tab + key; 
	
	pawn_hash->checksum = hash_key;
	pawn_hash->score = score;
	pawn_hash->pawnrec = pawnrec;
}
int SEARCHER::probe_pawn_hash(const HASHKEY& hash_key,SCORE& score,PAWNREC& pawnrec) {
	register PPROCESSOR proc = processors + processor_id;
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
    register PPROCESSOR proc = processors + processor_id;
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::eval_hash_tab_mask);
	register PEVALHASH eval_hash = proc->eval_hash_tab + key; 
	
	eval_hash->checksum = hash_key;
	eval_hash->score = (BMP16)score;
    eval_hash->lazy_score = (BMP16)lazy_score;
	eval_hash->evalrec = evalrec;
}
int SEARCHER::probe_eval_hash(const HASHKEY& hash_key,int& score,int& lazy_score,EVALREC& evalrec) {
	register PPROCESSOR proc = processors + processor_id;
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
