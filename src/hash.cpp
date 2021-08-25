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
void PROCESSOR::reset_hash_tab(int id,size_t size) {
    if(id != 0)
        return;

    if(size) hash_tab_mask = size - 1;
    else size = size_t(hash_tab_mask) + 1;
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

void PROCESSOR::reset_pawn_hash_tab(size_t size) {
    if(size) pawn_hash_tab_mask = size - 1;
    else size = size_t(pawn_hash_tab_mask) + 1;
    aligned_free<PAWNHASH>(pawn_hash_tab);
    aligned_reserve<PAWNHASH>(pawn_hash_tab,size);
}

void PROCESSOR::reset_eval_hash_tab(size_t size) {
    if(size) eval_hash_tab_mask = size - 1;
    else size = size_t(eval_hash_tab_mask) + 1;
    aligned_free<EVALHASH>(eval_hash_tab[white]);
    aligned_reserve<EVALHASH>(eval_hash_tab[white],2*size);
    eval_hash_tab[black] = eval_hash_tab[white] + size;
}

void PROCESSOR::delete_tables() {
    aligned_free<HASH>(hash_tab[white]);
    aligned_free<EVALHASH>(eval_hash_tab[white]);
    aligned_free<PAWNHASH>(pawn_hash_tab);
}

void PROCESSOR::clear_tables(int id) {
    /*clear main hashtable*/
    PPROCESSOR proc = processors[0];
    if(proc->hash_tab[white]) {
        size_t size = 2 * (size_t(hash_tab_mask) + 1);
        size_t dim =  size / n_processors;
        size_t n_entries = (id == n_processors - 1) ? (size - id * dim) : dim;
        PHASH addr = processors[0]->hash_tab[white] + dim * id;
        memset(addr,0,n_entries * sizeof(HASH));
    }
    /*clear local tables*/
    proc = processors[id];
    if(proc->eval_hash_tab[white])
        memset(proc->eval_hash_tab[white],0,
            2 * (size_t(PROCESSOR::eval_hash_tab_mask) + 1) * sizeof(EVALHASH));
    if(proc->pawn_hash_tab)
        memset((void*)proc->pawn_hash_tab,0,
            (size_t(PROCESSOR::pawn_hash_tab_mask) + 1) * sizeof(PAWNHASH));
}

/*
Clear hash tables in parallel
*/
static CDECL void clear_hash_proc(int id) {
    PROCESSOR::clear_tables(id);
}

void PROCESSOR::clear_hash_tables() {
#ifdef PARALLEL
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        processors[i]->state = PARK;
#endif

    int ncores = (montecarlo && SEARCHER::use_nn) ? 
            PROCESSOR::n_cores : PROCESSOR::n_processors;
    std::thread* mthreads = new std::thread[ncores];
    for(int id = 0; id < ncores; id++)
        mthreads[id] = t_create(clear_hash_proc,id);
    for(int id = 0; id < ncores; id++)
        t_join(mthreads[id]);
    delete[] mthreads;

#ifdef PARALLEL
    for(int i = 1;i < PROCESSOR::n_processors;i++) {
        processors[i]->state = WAIT;
        processors[i]->signal();
    }
#endif
}

/*
Main hash table
*/
#define HPROBES 4

void SEARCHER::record_hash(
                 int col,const HASHKEY& hash_key,int depth,int ply,int flags,
                 int eval,int score,MOVE move,int mate_threat,int singular
                 ) {
    PPROCESSOR proc = processors[0];
    PHASH addr,pslot,pr_slot = 0;
    HASH slot;
    int sc, max_sc = MAX_NUMBER;
    uint32_t check_sum = (uint32_t)(hash_key >> 32), s_data;
    uint32_t key = uint32_t(hash_key & PROCESSOR::hash_tab_mask);

    addr = proc->hash_tab[col];

    for(uint32_t i = 0; i < HPROBES; i++) {
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
    PPROCESSOR proc = processors[0];
    PHASH addr,pslot;
    HASH slot;
    int flags;
    uint32_t check_sum = (uint32_t)(hash_key >> 32), s_data;
    uint32_t key = uint32_t(hash_key & PROCESSOR::hash_tab_mask);

    addr = proc->hash_tab[col];
    
    for(uint32_t i = 0; i < HPROBES; i++) {
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
    /*main hashtable*/
    PPROCESSOR proc = processors[0];
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
