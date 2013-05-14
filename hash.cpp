#include "scorpio.h"

UBMP32 PROCESSOR::hash_tab_mask = (1 << 8) - 1;
UBMP32 PROCESSOR::eval_hash_tab_mask = (1 << 8) - 1;
UBMP32 PROCESSOR::pawn_hash_tab_mask = (1 << 8) - 1;
int PROCESSOR::age;

/*
Allocate tables 
	-Main hash table is shared.
	-The rest is allocated for each thread.
*/
void PROCESSOR::reset_hash_tab(int id,UBMP32 size) {
#if TT_TYPE == 0
	if(id != 0) return;
#endif
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
	for(int i = 0;i < n_processors;i++) {
		proc = processors[i];
		if(proc->white_hash_tab) {
			memset(proc->white_hash_tab,0,(hash_tab_mask + 1) * sizeof(HASH));
			memset(proc->black_hash_tab,0,(hash_tab_mask + 1) * sizeof(HASH));
		}
		memset(proc->pawn_hash_tab,0,(pawn_hash_tab_mask + 1) * sizeof(PAWNHASH));
		memset(proc->eval_hash_tab,0,(eval_hash_tab_mask + 1) * sizeof(EVALHASH));
	}
}

void PROCESSOR::delete_hash_tables() {
	aligned_free<HASH>(white_hash_tab);
	aligned_free<HASH>(black_hash_tab);
	aligned_free<PAWNHASH>(pawn_hash_tab);
	aligned_free<EVALHASH>(eval_hash_tab);
}
/*
Main hash table
*/
#define HPROBES 4

void SEARCHER::record_hash(
				 int col,const HASHKEY& hash_key,int depth,int ply,
				 int flags,int score,MOVE move,int mate_threat,int singular
				 ) {
#if TT_TYPE == 0
	static const PPROCESSOR proc = processors[0];
	UBMP32 key = UBMP32(hash_key & PROCESSOR::hash_tab_mask);
#elif TT_TYPE == 1
	PPROCESSOR proc = processors[(hash_key % PROCESSOR::n_processors)];
	UBMP32 key = UBMP32((hash_key / PROCESSOR::n_processors) & PROCESSOR::hash_tab_mask);
#else
	PPROCESSOR proc = processors[processor_id];
	UBMP32 key = UBMP32(hash_key & PROCESSOR::hash_tab_mask);
#endif
	register PHASH addr,pslot,pr_slot = 0;
	register HASH slot;
	int sc,max_sc = MAX_NUMBER;

	if(col == white) 
		addr = proc->white_hash_tab;
	else 
		addr = proc->black_hash_tab;

	for(int i = 0; i < HPROBES; i++) {
		pslot = (addr + (key ^ i));    //H.G trick to follow most probable path
		slot = *pslot;
		if(!slot.hash_key || (slot.hash_key ^ slot.data_key) == hash_key) {
			if(flags == HASH_HIT && slot.move == move) 
				return;
			if((slot.depth > depth) && flags != CRAP && ((slot.flags & 3) + EXACT != CRAP))
				return;
			if(score > WIN_SCORE) 
				score += WIN_PLY * (ply + 1);
			else if(score < -WIN_SCORE) 
				score -= WIN_PLY * (ply + 1);
			
			if(!move) move = slot.move;
			mate_threat = (mate_threat || (slot.flags & 4));
			pr_slot = pslot;
			break;
		} else {
			sc = (slot.flags & ~7)                     //8 * age   {larger weight}
				+  DEPTH(slot.depth)                   //depth     {non-fractional depth}
				+ ((slot.flags & 3) ? 0 : (4 << 3));   //8 * 4     {EXACT score goes 4 ages up}
			if(sc < max_sc) {
				pr_slot = pslot;
				max_sc = sc;
			}
		}
	}
	if(flags == HASH_HIT) flags = CRAP;
	slot.move = UBMP32(move);
	slot.score = BMP16(score);
	slot.depth = UBMP8(depth);
	slot.flags = UBMP8((flags - EXACT) | (mate_threat << 2) | (singular << 3) | (PROCESSOR::age << 4));
	slot.hash_key = (hash_key ^ slot.data_key);
	*pr_slot = slot;
}

int SEARCHER::probe_hash(
			   int col,const HASHKEY& hash_key,int depth,int ply,int& score,
			   MOVE& move,int alpha,int beta,int& mate_threat,int& singular,int& h_depth,
			   bool exclusiveP
			   ) {
#if TT_TYPE == 0
	static const PPROCESSOR proc = processors[0];
	UBMP32 key = UBMP32(hash_key & PROCESSOR::hash_tab_mask);
#elif TT_TYPE == 1
	PPROCESSOR proc = processors[(hash_key % PROCESSOR::n_processors)];
	UBMP32 key = UBMP32((hash_key / PROCESSOR::n_processors) & PROCESSOR::hash_tab_mask);
#else
	PPROCESSOR proc = processors[processor_id];
	UBMP32 key = UBMP32(hash_key & PROCESSOR::hash_tab_mask);
#endif
	register PHASH addr,pslot;
	register HASH slot;
    register int flags;
	
	if(col == white) 
		addr = proc->white_hash_tab;
	else 
		addr = proc->black_hash_tab;
	
	for(int i = 0; i < HPROBES; i++) {
		pslot = addr + (key ^ i);
		slot = *pslot;
		if((slot.hash_key ^ slot.data_key) == hash_key) {
			score = slot.score;
			if(score > WIN_SCORE) 
				score -= WIN_PLY * (ply + 1);
			else if(score < -WIN_SCORE) 
				score += WIN_PLY * (ply + 1);
			
			move = slot.move;
			mate_threat |= ((slot.flags >> 2) & 1);
			singular = ((slot.flags >> 3) & 1);
			flags = (slot.flags & 3) + EXACT;
			h_depth = slot.depth;
			
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

			if(flags == CRAP)
				return CRAP;

			if(exclusiveP) {
				slot.hash_key ^= slot.data_key;
				slot.flags |= 3;
				slot.depth = 255;
				slot.hash_key ^= slot.data_key;
				*pslot = slot;
			}

			return HASH_HIT;
		}
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
	
	pawn_hash->hash_key = hash_key;
	pawn_hash->score = score;
	pawn_hash->pawnrec = pawnrec;
}
int SEARCHER::probe_pawn_hash(const HASHKEY& hash_key,SCORE& score,PAWNREC& pawnrec) {
	register PPROCESSOR proc = processors[processor_id];
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::pawn_hash_tab_mask);
	register PPAWNHASH pawn_hash = proc->pawn_hash_tab + key; 
	
	if(pawn_hash->hash_key == hash_key) {
		score = pawn_hash->score;
		pawnrec = pawn_hash->pawnrec;
		return 1;
	}
	return 0;
}
/*
Eval hash tables
*/
void SEARCHER::record_eval_hash(const HASHKEY& hash_key,int score) {
	register PPROCESSOR proc = processors[processor_id];
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::eval_hash_tab_mask);
	register PEVALHASH pslot = proc->eval_hash_tab + key; 

	pslot->check_sum = (UBMP32)(hash_key >> 32);
	pslot->score = (BMP16)score;
}
int SEARCHER::probe_eval_hash(const HASHKEY& hash_key,int& score) {
	register PPROCESSOR proc = processors[processor_id];
	register UBMP32 key = UBMP32(hash_key & PROCESSOR::eval_hash_tab_mask);
	register PEVALHASH pslot = proc->eval_hash_tab + key; 

	if(pslot->check_sum == (UBMP32)(hash_key >> 32)) {
		score = pslot->score;
		return 1;
	}
	return 0;
}
/*
prefetch tt
*/
void SEARCHER::prefetch_tt() {
#ifdef HAS_PREFETCH
#if TT_TYPE == 0
	static const PPROCESSOR proc = processors[0];
	UBMP32 key = UBMP32(hash_key & PROCESSOR::hash_tab_mask);
#elif TT_TYPE == 1
	PPROCESSOR proc = processors[(hash_key % PROCESSOR::n_processors)];
	UBMP32 key = UBMP32((hash_key / PROCESSOR::n_processors) & PROCESSOR::hash_tab_mask);
#else
	PPROCESSOR proc = processors[processor_id];
	UBMP32 key = UBMP32(hash_key & PROCESSOR::hash_tab_mask);
#endif	
	if(player == white) {
		PREFETCH_T0(proc->white_hash_tab + key);
	} else { 
		PREFETCH_T0(proc->black_hash_tab + key);
	}
#endif
}
void SEARCHER::prefetch_qtt() {
#ifdef HAS_PREFETCH
	PPROCESSOR proc = processors[processor_id];
	UBMP32 key;
	key = UBMP32(pawn_hash_key & PROCESSOR::pawn_hash_tab_mask);
	PREFETCH_T0(proc->pawn_hash_tab + key);
	key = UBMP32(hash_key & PROCESSOR::eval_hash_tab_mask);
	PREFETCH_T0(proc->eval_hash_tab + key); 
#endif
}
