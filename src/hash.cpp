#include "scorpio.h"

uint32_t PROCESSOR::hash_tab_mask = (1 << 8) - 1;
uint32_t PROCESSOR::eval_hash_tab_mask = (1 << 8) - 1;
uint32_t PROCESSOR::pawn_hash_tab_mask = (1 << 8) - 1;
int PROCESSOR::age;

/*
Allocate tables 
    -Main hash table is shared.
    -The rest is allocated for each thread.
*/
void PROCESSOR::reset_hash_tab(int id,uint32_t size) {
#if NUMA_TT_TYPE == 0
    if(id != 0) return;
#endif
    if(size) hash_tab_mask = size - 1;
    else size = hash_tab_mask + 1;
    /*page size*/
#if defined(__ANDROID__) || defined(_WIN32)
    static const int alignment = 4096;
#else
    static const int alignment = 2 * 1024 * 1024;
#endif
    aligned_free<HASH>(hash_tab[white]);
    aligned_reserve<HASH,alignment,true>(hash_tab[white],2*size);
    hash_tab[black] = hash_tab[white] + size;
}

void PROCESSOR::reset_pawn_hash_tab(uint32_t size) {
    if(size) pawn_hash_tab_mask = size - 1;
    else size = pawn_hash_tab_mask + 1;
    aligned_free<PAWNHASH>(pawn_hash_tab);
    aligned_reserve<PAWNHASH>(pawn_hash_tab,size);
}

void PROCESSOR::reset_eval_hash_tab(uint32_t size) {
    if(size) eval_hash_tab_mask = size - 1;
    else size = eval_hash_tab_mask + 1;
    aligned_free<EVALHASH>(eval_hash_tab[white]);
    aligned_reserve<EVALHASH>(eval_hash_tab[white],2*size);
    eval_hash_tab[black] = eval_hash_tab[white] + size;
}

void PROCESSOR::clear_hash_tables() {
    PPROCESSOR proc;
    for(int i = 0;i < n_processors;i++) {
        proc = processors[i];
        if(proc->hash_tab[white])
            memset(proc->hash_tab[white],0,2 * (hash_tab_mask + 1) * sizeof(HASH));
        if(proc->eval_hash_tab[white])
            memset(proc->eval_hash_tab[white],0,2 * (eval_hash_tab_mask + 1) * sizeof(EVALHASH));
        if(proc->pawn_hash_tab)
            memset((void*)proc->pawn_hash_tab,0,(pawn_hash_tab_mask + 1) * sizeof(PAWNHASH));
    }
}

void PROCESSOR::delete_hash_tables() {
    aligned_free<HASH>(hash_tab[white]);
    aligned_free<EVALHASH>(eval_hash_tab[white]);
    aligned_free<PAWNHASH>(pawn_hash_tab);
}

/* 
 Distributed hashtable on NUMA machines
 */
#if NUMA_TT_TYPE == 0
#define TT_PROC \
    PPROCESSOR proc = processors[0];
#elif NUMA_TT_TYPE == 1
#define TT_PROC \
    PPROCESSOR proc = processors[((hash_key >> 48) * PROCESSOR::n_processors) >> 16];
#else
#define TT_PROC \
    PPROCESSOR proc = processors[processor_id];
#endif
/*
Main hash table
*/
#define HPROBES 4

void SEARCHER::record_hash(
                 int col,const HASHKEY& hash_key,int depth,int ply,int flags,
                 int eval,int score,MOVE move,int mate_threat,int singular
                 ) {
    TT_PROC;
    PHASH addr,pslot,pr_slot = 0;
    HASH slot;
    int sc, max_sc = MAX_NUMBER;
    uint32_t check_sum = (uint32_t)(hash_key >> 32), s_data;
    uint32_t key = uint32_t(hash_key & PROCESSOR::hash_tab_mask);

    addr = proc->hash_tab[col];

    for(int i = 0; i < HPROBES; i++) {
        pslot = (addr + (key ^ i));    //H.G trick to follow most probable path
        slot = *pslot;
        s_data = (slot.move ^ slot.eval ^ slot.data);

        if(!slot.check_sum || (slot.check_sum ^ s_data) == check_sum) {
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
            sc = (slot.flags & 0xf0)                   //16 * age  {largest weight}
                +  slot.depth                          //depth
                + ((slot.flags & 3) ? 0 : (4 << 4));   //16 * 4    {EXACT score goes 4 ages up}
            if(sc < max_sc) {
                pr_slot = pslot;
                max_sc = sc;
            }
        }
    }

    if(flags == HASH_HIT) flags = CRAP;
    slot.move  = MOVE(move);
    slot.score = int16_t(score);
    slot.depth = uint8_t(depth);
    slot.flags = uint8_t((flags - EXACT) | (mate_threat << 2) | (singular << 3) | (PROCESSOR::age << 4));
    slot.eval = int32_t(eval);
    s_data = (slot.move ^ slot.eval ^ slot.data);
    slot.check_sum = (check_sum ^ s_data);
    *pr_slot = slot;
}

int SEARCHER::probe_hash(
               int col,const HASHKEY& hash_key,int depth,int ply,int& eval,int& score,
               MOVE& move,int alpha,int beta,int& mate_threat,int& singular,int& h_depth,
               bool exclusiveP
               ) {
    TT_PROC;
    PHASH addr,pslot;
    HASH slot;
    int flags;
    uint32_t check_sum = (uint32_t)(hash_key >> 32), s_data;
    uint32_t key = uint32_t(hash_key & PROCESSOR::hash_tab_mask);

    addr = proc->hash_tab[col];
    
    for(int i = 0; i < HPROBES; i++) {
        pslot = addr + (key ^ i);
        slot = *pslot;
        s_data = (slot.move ^ slot.eval ^ slot.data);

        if((slot.check_sum ^ s_data) == check_sum) {
            score = slot.score;
            if(score > WIN_SCORE) 
                score -= WIN_PLY * (ply + 1);
            else if(score < -WIN_SCORE) 
                score += WIN_PLY * (ply + 1);
            
            eval = slot.eval;
            move = slot.move;
            mate_threat |= ((slot.flags >> 2) & 1);
            singular = ((slot.flags >> 3) & 1);
            flags = (slot.flags & 3) + EXACT;
            h_depth = slot.depth;

            if(flags == CRAP)
                return CRAP;
            
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

            if(depth - 4 <= h_depth 
                && (flags == UPPER && score < beta))
                return AVOID_NULL;

            if(depth - 4 <= h_depth
                && ( (flags == EXACT && score > alpha) 
                  || (flags == LOWER && score >= beta)))
                return HASH_GOOD;

            if(exclusiveP) {
                slot.check_sum ^= slot.data;
                slot.flags |= (CRAP - EXACT);
                slot.depth = 255;
                slot.check_sum ^= slot.data;
                *pslot = slot;
            }

            return HASH_HIT;
        }
    }

    return UNKNOWN;
}
/*
Compute how full the main hash table is
*/
int PROCESSOR::hashfull() {
    int count = 0;
    PHASH addr = processors[0]->hash_tab[0];
    for(int i = 0; i < 1000; i++) {
        if(addr[i].check_sum != 0 && 
            (addr[i].flags >> 4) == PROCESSOR::age)
            count++;
    }
    return count;
}
/*
Pawn hash tables
*/
void SEARCHER::record_pawn_hash(const HASHKEY& hash_key,const SCORE& score,const PAWNREC& pawnrec) {
    PPROCESSOR proc = processors[processor_id];
    uint32_t key = uint32_t(hash_key & PROCESSOR::pawn_hash_tab_mask);
    PPAWNHASH pawn_hash = proc->pawn_hash_tab + key; 
    
    pawn_hash->check_sum = (uint16_t)(hash_key >> 32);
    pawn_hash->score = score;
    pawn_hash->pawnrec = pawnrec;
}
int SEARCHER::probe_pawn_hash(const HASHKEY& hash_key,SCORE& score,PAWNREC& pawnrec) {
    PPROCESSOR proc = processors[processor_id];
    uint32_t key = uint32_t(hash_key & PROCESSOR::pawn_hash_tab_mask);
    PPAWNHASH pawn_hash = proc->pawn_hash_tab + key; 
    
    if(pawn_hash->check_sum == (uint16_t)(hash_key >> 32)) {
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
    PPROCESSOR proc = processors[processor_id];
    uint32_t key = uint32_t(hash_key & PROCESSOR::eval_hash_tab_mask);
    PEVALHASH pslot = proc->eval_hash_tab[player] + key; 

    pslot->check_sum = (uint16_t)(hash_key >> 32);
    pslot->score = (int16_t)score;
}
int SEARCHER::probe_eval_hash(const HASHKEY& hash_key,int& score) {
    PPROCESSOR proc = processors[processor_id];
    uint32_t key = uint32_t(hash_key & PROCESSOR::eval_hash_tab_mask);
    PEVALHASH pslot = proc->eval_hash_tab[player] + key; 

    if(pslot->check_sum == (uint16_t)(hash_key >> 32)) {
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
    TT_PROC;
    if(proc->hash_tab[white]) {
        uint32_t key = uint32_t(hash_key & PROCESSOR::hash_tab_mask);
        PREFETCH_T0(proc->hash_tab[player] + key);
    }
    /*eval/pawn hashtable*/
    proc = processors[processor_id];
    if(proc->pawn_hash_tab) {
        uint32_t key = uint32_t(pawn_hash_key & PROCESSOR::pawn_hash_tab_mask);
        PREFETCH_T0(proc->pawn_hash_tab + key);
    }
    if(proc->hash_tab[white]) {
        uint32_t key = uint32_t(hash_key & PROCESSOR::eval_hash_tab_mask);
        PREFETCH_T0(proc->eval_hash_tab + key);
    }
#endif
}
