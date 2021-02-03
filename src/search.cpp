#include "scorpio.h"

static const int CHECK_DEPTH = UNITDEPTH;
static int use_probcut = 0;
static int use_singular = 1;
static int singular_margin = 32;
static int probcut_margin = 195;
static int prev_pv_length;
static int alphabeta_man_c = 6;
static int multipv = 0;
int qsearch_level = 0;

/* search options */
const int use_nullmove = 1;
const int use_selective = 1;
const int use_tt = 1;
const int use_aspiration = 1;
const int use_iid = 1;
const int use_pvs = 1;
int contempt = 10;

/* parameter */
#ifdef TUNE
#   define PARAM int
#else
#   define PARAM const int
#endif

static PARAM aspiration_window = 10;
static PARAM futility_margin[] = {0, 143, 232, 307, 615, 703, 703, 960};
static PARAM failhigh_margin[] = {0, 126, 304, 382, 620, 725, 1280, 0};
static PARAM razor_margin[] = {0, 136, 181, 494, 657, 0, 0, 0};
static PARAM lmp_count[] = {0, 10, 10, 15, 21, 24, 44, 49};
static PARAM lmr_count[] = {4, 6, 7, 8, 13, 20, 25, 31};
static PARAM lmr_all_count = 3;
static PARAM lmr_cut_count = 5;
static PARAM lmr_root_count[] = {4, 8};

/*
* Update pv
*/
FORCEINLINE void SEARCHER::UPDATE_PV(MOVE move)  {
    pstack->pv[ply] = move; 
    memcpy(&pstack->pv[ply + 1] , &(pstack + 1)->pv[ply + 1] , 
        ((pstack + 1)->pv_length - ply) * sizeof(MOVE));
    pstack->pv_length = (pstack + 1)->pv_length;
}
/*
* hashtable cutoff
*/
bool SEARCHER::hash_cutoff() { 
    /*abdada*/
    bool exclusiveP = false;
#ifdef PARALLEL
    if(!montecarlo && (use_abdada_smp == 1) && ply > 1
        && PROCESSOR::n_processors > 1
        && DEPTH((pstack - 1)->depth) > PROCESSOR::SMP_SPLIT_DEPTH
        && !(pstack - 1)->second_pass
        && (pstack - 1)->legal_moves > 1
        ) {
        exclusiveP = true;
    }
#endif

    /*probe*/
    pstack->hash_flags = PROBE_HASH(player,hash_key,pstack->depth,ply,pstack->hash_score,
        pstack->hash_move,pstack->alpha,pstack->beta,pstack->mate_threat,
        pstack->singular,pstack->hash_depth,exclusiveP);

#ifdef PARALLEL
    if(exclusiveP) {
        if(pstack->hash_flags == CRAP) {
            /*move is taken!*/
            pstack->best_score = SKIP_SCORE;
            return true;
        } else if(pstack->hash_flags == HASH_HIT) {
            /*we had a hit and replaced the flag with load of CRAP (depth=255)*/
        } else if(pstack->hash_flags == UNKNOWN) {
            /*store new crap*/
            RECORD_HASH(player,hash_key,255,0,CRAP,0,0,0,0);
        }
    }
#endif

#if 1
    /*legality checking*/
    if(pstack->hash_move
        && !is_legal_fast(pstack->hash_move)
        ) {
        pstack->hash_move = 0;
    }
#endif

    /*cutoff*/
    if(pstack->singular && pstack->hash_flags != HASH_GOOD)
        pstack->singular = 0;
    if((pstack->node_type != PV_NODE || (ply > 0 && pstack->hash_depth > pstack->depth))
        && pstack->hash_flags >= EXACT
        && pstack->hash_flags <= LOWER
        ) {
        if(pstack->hash_move) {
            pstack->pv_length = ply + 1;
            pstack->pv[ply] = pstack->hash_move;
        }
        pstack->best_move = pstack->hash_move;
        pstack->best_score = pstack->hash_score;
        return true;
    }

    return false;
}
FORCEINLINE int SEARCHER::on_node_entry() {
    
    /*qsearch?*/
    if(pstack->depth <= 0) {
        if(qsearch_level < 0)
            pstack->qcheck_depth = 0;
        else if(pstack->node_type == PV_NODE) 
            pstack->qcheck_depth = 2 * CHECK_DEPTH; 
        else 
            pstack->qcheck_depth = CHECK_DEPTH;
        qsearch_nn();
        return true;
    }

    /*prefetch*/
    prefetch_tt();

    /*razoring & static pruning*/
    if(use_selective
        && all_man_c > MAX_EGBB
        && pstack->depth <= 6 * UNITDEPTH
        && (pstack - 1)->search_state != NULL_MOVE
        && !pstack->extension
        && pstack->node_type != PV_NODE
        ) {
            int score = eval(true);
            int rmargin = razor_margin[DEPTH(pstack->depth)];
            int fhmargin = failhigh_margin[DEPTH(pstack->depth)];

            if(score + rmargin <= pstack->alpha
                && pstack->depth <= 4 * UNITDEPTH
                ) {
                pstack->qcheck_depth = CHECK_DEPTH;
                /*do qsearch with shifted-down window*/
                {
                    int palpha = pstack->alpha;
                    int pbeta = pstack->beta;
                    pstack->alpha -= rmargin;
                    pstack->beta = pstack->alpha + 1;
                    qsearch_nn();
                    pstack->alpha = palpha;
                    pstack->beta = pbeta;
                }
                score = pstack->best_score;
                if(score + rmargin <= pstack->alpha)
                    return true;
            } else if(score - fhmargin >= pstack->beta) {
                pstack->best_score = score - fhmargin;
                return true;
            }
    }

    /*initialize node*/
    pstack->gen_status = GEN_START;
    pstack->flag = UPPER;
    pstack->pv_length = ply;
    pstack->legal_moves = 0;
    pstack->hash_move = 0;
    pstack->best_score = -MATE_SCORE;
    pstack->best_move = 0;
    pstack->mate_threat = 0;
    pstack->singular = 0;
    pstack->all_done = true;
    pstack->second_pass = false;
    
    if(pstack->node_type == PV_NODE) {
        pstack->next_node_type = PV_NODE;
    } else if(pstack->node_type == ALL_NODE) {
        pstack->next_node_type = CUT_NODE;
    } else {
        pstack->next_node_type = ALL_NODE;
    }
    
    /*root*/
    if(!ply) {
        pstack->gen_status = GEN_RESET;
        return false;
    }

    if(pstack->alpha > MATE_SCORE - WIN_PLY * (ply)) {
        pstack->best_score = pstack->alpha;
        return true; 
    }

    if(draw()) {
        pstack->best_score = 
            ((scorpio == player) ? -contempt : contempt);
        return true;
    }

    if(ply >= MAX_PLY - 1) {
        pstack->best_score = eval();
        return true;
    }

    /*
    * Processor 0 of host 0 does polling for input
    * from keypress and checks for time limit.
    */
    if(processor_id == 0 CLUSTER_CODE(&& PROCESSOR::host_id == 0)) {
        if(nodes > time_check + poll_nodes) {
            time_check = nodes;
            if(!abort_search)
                check_quit();
            if(abort_search CLUSTER_CODE(&& !use_abdada_cluster)) {
                CLUSTER_CODE(PROCESSOR::quit_hosts());
                return true;
            }
        } 
    }

    /*check for messages from other hosts*/
#ifdef CLUSTER
#   ifndef THREAD_POLLING
    if(processor_id == 0 && 
       nodes > message_check + PROCESSOR::MESSAGE_POLL_NODES) {
        processors[processor_id]->idle_loop();
        message_check = nodes;
    }
#   endif
#endif   

    /*probe hash table*/
    if(use_tt && hash_cutoff())
        return true;

    /*bitbase cutoff*/
    if(bitbase_cutoff())
        return true;

    return false;
}

FORCEINLINE int SEARCHER::on_qnode_entry() {
    int score;

    /*prefetch*/
    prefetch_tt();
    prefetch_qtt();

    /*initialize node*/
    pstack->gen_status = GEN_START;
    pstack->pv_length = ply;
    pstack->best_score = -MATE_SCORE;
    pstack->best_move = 0;
    pstack->legal_moves = 0;
    pstack->hash_move = 0;
    pstack->flag = UPPER;

    if(pstack->node_type == PV_NODE) {
        pstack->next_node_type = PV_NODE;
    } else if(pstack->node_type == ALL_NODE) {
        pstack->next_node_type = CUT_NODE;
    } else {
        pstack->next_node_type = ALL_NODE;
    }

    if(pstack->alpha > MATE_SCORE - WIN_PLY * ply) {
        pstack->best_score = pstack->alpha;
        return true; 
    }

    if(draw()) {
        pstack->best_score = 
            ((scorpio == player) ? -contempt : contempt);
        return true;
    }

    if(ply >= MAX_PLY - 1) {
        pstack->best_score = eval();
        return true;
    }

    /*probe hash table*/
    if(use_tt && hash_cutoff())
        return true;

    /*stand pat*/
    if((hply >= 1 && !hstack[hply - 1].checks) || !ply) {
        score = eval();
        pstack->best_score = score;
        if(score > pstack->alpha) {
            if(score >= pstack->beta)
                return true;
            pstack->alpha = score;
        }
    }

    return false;
}
/*pawn push*/
int SEARCHER::is_pawn_push(MOVE move) const {
    if(PIECE(m_piece(move)) == pawn) {
        if(opponent == white) {
            if(rank(m_from(move)) >= RANK5) return 1;
        } else {
            if(rank(m_from(move)) <= RANK4) return 1;
        }
    }
    return 0;
}

/* Extend/reduce macros */
#define extend(value) {                      \
    extension += value;                      \
    pstack->extension = 1;                   \
}

#define reduce(value) {                      \
    pstack->depth -= value;                  \
    pstack->reduction += value;              \
}

/*selective search*/
int SEARCHER::be_selective(int nmoves, bool mc) {
    MOVE move = hstack[hply - 1].move; 
    int extension = 0,score,depth = DEPTH((pstack - 1)->depth);
    int node_t = (pstack - 1)->node_type;

    pstack->extension = 0;
    pstack->reduction = 0;

    /*root*/
    if(ply == 1) {
        if(nmoves >= lmr_root_count[0]
            && !hstack[hply - 1].checks
            && !is_cap_prom(move)
            && !is_pawn_push(move)
            ) {
            reduce(UNITDEPTH);
            if(nmoves >= lmr_root_count[1] && pstack->depth > UNITDEPTH)
                reduce(UNITDEPTH);
        }
        return false;
    }
    /*
    non-cap phase
    */
    bool noncap_reduce, loscap_reduce;
    if(!mc) {
        bool avail_noncap = (pstack - 1)->gen_status == GEN_AVAIL && 
                        (pstack - 1)->current_index - 1 >= (pstack - 1)->noncap_start;
        noncap_reduce = ((pstack - 1)->gen_status - 1 == GEN_KILLERS ||
                         (pstack - 1)->gen_status - 1 == GEN_NONCAPS ||
                         (avail_noncap && !is_cap_prom(move)));
        loscap_reduce = ((pstack - 1)->gen_status - 1 == GEN_LOSCAPS ||
                         (avail_noncap && is_cap_prom(move)));
    } else {
        noncap_reduce = (!is_cap_prom(move));
        loscap_reduce = (is_cap_prom(move) && see(move) < 0);
    }

    /*
    extend
    */
    if(hstack[hply - 1].checks) {
        if(node_t == PV_NODE) { extend(UNITDEPTH); }
        else { extend(0); }
    }
    if(is_pawn_push(move)) { 
        extend(0);
    }
    if(m_capture(move)
        && piece_c[white] + piece_c[black] == 0
        && PIECE(m_capture(move)) != pawn
        ) {
        extend(UNITDEPTH);
    }
    if((pstack - 1)->singular && nmoves == 1) {
        extend(UNITDEPTH);
    }
    if(extension > UNITDEPTH)
        extension = UNITDEPTH;

    pstack->depth += extension; 
    /*
    pruning
    */
    if(depth <= 7
        && all_man_c > MAX_EGBB
        && !pstack->extension
        && noncap_reduce
        && node_t != PV_NODE
        && ABS((pstack - 1)->best_score) != MATE_SCORE
        ) {
            //late move
            if(nmoves >= lmp_count[depth])
                return true;

            //see
            if(see(move) <= -100 * depth)
                return true;

            //futility
            int margin = futility_margin[depth];
            margin = MAX(margin / 4, margin - 10 * nmoves);
            score = -eval(true);
            if(score + margin < (pstack - 1)->alpha) {
                if(score > (pstack - 1)->best_score) {
                    (pstack - 1)->best_score = score;
                    (pstack - 1)->best_move = move;
                }
                return true;
            }
    }
    /*
    late move reduction
    */
    if(noncap_reduce && nmoves >= 2) {
        //by number of moves searched so far including current move
        for(int i = 0;i < 8;i++) {
            if(nmoves >= lmr_count[i] && pstack->depth > UNITDEPTH) {
                reduce(UNITDEPTH);
            }
        }
        //lets find more excuses to reduce
        //all and cut nodes
        if( ( (node_t == ALL_NODE && nmoves >= lmr_all_count) ||
              (node_t == CUT_NODE && nmoves >= lmr_cut_count) )
            && pstack->depth > UNITDEPTH
            ) { 
            reduce(UNITDEPTH);
            if(nmoves >= 8 && pstack->depth > UNITDEPTH) { reduce(UNITDEPTH); }
        }
        //pv is capture move
        if((pstack-1)->hash_move == (pstack-1)->best_move &&
            is_cap_prom((pstack-1)->hash_move) && pstack->depth > UNITDEPTH) {
            reduce(UNITDEPTH);
            if(nmoves >= 8 && pstack->depth > UNITDEPTH) { reduce(UNITDEPTH); }
        }
        //see reduction
        if(depth > 7 && pstack->depth > UNITDEPTH && see(move) < 0) {
            reduce(UNITDEPTH);
            if(nmoves >= 8 && pstack->depth > UNITDEPTH) { reduce(UNITDEPTH); }
        }
        //reduce extended moves less
        if(pstack->extension) {
            reduce(-pstack->reduction / 2);
        }
    }
    /*losing captures*/
    if(loscap_reduce && nmoves >= 2) {
        if(pstack->extension) {
            reduce(UNITDEPTH);
        } else if(pstack->depth <= 2 * UNITDEPTH)
            return true;
        else if(pstack->depth > 4 * UNITDEPTH) {
            reduce(pstack->depth / 2);
        } else {
            reduce(2 * UNITDEPTH);
        }
    }
    /* slow neural network pruning*/
    if(use_nn && !skip_nn && nmoves >= 3) {
        reduce(4 * UNITDEPTH);
        if(pstack->depth <= 0)
            return true;
    }
    /*
    end
    */
    return false;
}

/*
Back up to previous ply
*/
#define GOBACK(save) {                                                                                          \
    if(use_tt && save && !sb->abort_search && !sb->stop_searcher &&                                             \
        ((sb->pstack->search_state & ~MOVE_MASK) != SINGULAR_SEARCH)) {                                         \
        sb->RECORD_HASH(sb->player,sb->hash_key,sb->pstack->depth,sb->ply,sb->pstack->flag,                     \
        sb->pstack->best_score,sb->pstack->best_move,sb->pstack->mate_threat,sb->pstack->singular);             \
    }                                                                                                           \
    if(sb->pstack->search_state & ~MOVE_MASK) goto SPECIAL;                                                     \
    goto POP;                                                                                                   \
};
/*
Iterative search.
*/
#ifdef PARALLEL
void search(PROCESSOR* const proc, bool single)
#else
void search(SEARCHER* const sb, bool single)
#endif
{
    MOVE move;
    int score = 0;
#ifdef PARALLEL
    PSEARCHER sb = proc->searcher;
    int active_workers;
    CLUSTER_CODE(int active_hosts);
#endif
    /*
    * First processor goes on searching, while the
    * rest go to sleep mode.
    */
    if(sb SMP_CODE(&& proc->state == GO)) {
        sb->stop_ply = sb->ply;
        goto NEW_NODE;
    } else {
        SMP_CODE(goto IDLE_START);
    }

    /*
    * Iterative depth first search (DFS)
    */
    while(true) {
        /*
        * GO forward pushing moves until tip of tree is reached
        */
        while(true) {
START:
            /*
            * IID and Singular search. These two are related in a way because
            * we do IID at every node to get a move to be tested for singularity later.
            * Try both at every node other than _ALL_ nodes. Use moderate reduction for IID
            * so that we have a good enough move. In the test for singularity reduce the depth
            * further by 2 plies.
            */
            switch(sb->pstack->search_state) {
            case PROBCUT_SEARCH:
                if(use_probcut
                    && sb->pstack->depth >= 6 * UNITDEPTH
                    && sb->pstack->node_type != PV_NODE
                    ) {
                    sb->pstack->o_alpha = sb->pstack->alpha;
                    sb->pstack->o_beta = sb->pstack->beta;
                    sb->pstack->o_depth = sb->pstack->depth;
                    if(sb->pstack->node_type == CUT_NODE)
                        sb->pstack->alpha = sb->pstack->beta + probcut_margin;
                    else
                        sb->pstack->alpha = sb->pstack->alpha - probcut_margin;
                    sb->pstack->beta = sb->pstack->alpha + 1;
                    sb->pstack->depth -= 4 * UNITDEPTH;
                    sb->pstack->search_state |= NORMAL_MOVE; 
                } else {
                    sb->pstack->search_state = IID_SEARCH;
                    goto START;
                }
                break;
            case IID_SEARCH:
                if(use_iid
                    && sb->pstack->node_type != ALL_NODE
                    && !sb->pstack->hash_move
                    && sb->pstack->depth >= 6 * UNITDEPTH
                    ) {
                    sb->pstack->o_alpha = sb->pstack->alpha;
                    sb->pstack->o_beta = sb->pstack->beta;
                    sb->pstack->o_depth = sb->pstack->depth;
                    if(sb->pstack->node_type == PV_NODE) 
                        sb->pstack->depth -= 2 * UNITDEPTH;
                    else 
                        sb->pstack->depth -= 4 * UNITDEPTH;
                    sb->pstack->search_state |= NORMAL_MOVE; 
                } else {
                    sb->pstack->search_state = SINGULAR_SEARCH;
                    goto START;
                }
                break;
            case SINGULAR_SEARCH:
                if(use_singular 
                    && sb->pstack->node_type != ALL_NODE
                    && sb->pstack->hash_move 
                    && sb->pstack->depth >= 8 * UNITDEPTH
                    && sb->pstack->hash_flags == HASH_GOOD
                    && !sb->pstack->singular
                    ) {
                        sb->pstack->o_alpha = sb->pstack->alpha;
                        sb->pstack->o_beta = sb->pstack->beta;
                        sb->pstack->o_depth = sb->pstack->depth;
                        sb->pstack->alpha = sb->pstack->hash_score - singular_margin;
                        sb->pstack->beta = sb->pstack->alpha + 1;
                        sb->pstack->depth -= 6 * UNITDEPTH;
                        sb->pstack->search_state |= NORMAL_MOVE; 
                } else {
                    sb->pstack->search_state = NORMAL_MOVE;
                    goto START;
                }
                break;
            }
            /*
            * Get a legal move (normal/null) and play it on the board.
            * Null move implementation is clearer this way.
            */
            switch(sb->pstack->search_state & MOVE_MASK) {
            case NULL_MOVE:
                if(use_nullmove
                    && !sb->hstack[sb->hply - 1].checks
                    && sb->pstack->hash_flags != AVOID_NULL
                    && sb->pstack->depth >= 4 * UNITDEPTH
                    && sb->pstack->node_type != PV_NODE
                    && sb->piece_c[sb->player]
                    && (score = (sb->eval(true) - sb->pstack->beta)) >= 0
                    ) {
                        sb->PUSH_NULL();
                        sb->pstack->extension = 0;
                        sb->pstack->reduction = 0;
                        sb->pstack->alpha = -(sb->pstack - 1)->beta;
                        sb->pstack->beta = -(sb->pstack - 1)->beta + 1;
                        sb->pstack->node_type = (sb->pstack - 1)->next_node_type;
                        /* Smooth scaling from Dann Corbit based on score and depth*/
                        sb->pstack->depth = (sb->pstack - 1)->depth - 3 * UNITDEPTH - 
                                            (sb->pstack - 1)->depth / 4 -
                                            (MIN(3 , score / 128) * UNITDEPTH);
                        /*search normal move after null*/
                        sb->pstack->search_state = NORMAL_MOVE;
                        goto NEW_NODE;
                }
                sb->pstack->search_state = PROBCUT_SEARCH;
                goto START;
            case NORMAL_MOVE:
                while(true) {
#ifdef PARALLEL
                    /*
                    * Get Smp move
                    */
                    if(!use_abdada_smp && !single
                        && sb->master && sb->stop_ply == sb->ply) {
                        if(!sb->get_smp_move())
                            GOBACK(false); //I
                    } else
#endif
                    /*
                    * Get a move in the regular manner
                    */
                    {
                        if(!sb->get_move()) {
                            if(!sb->pstack->legal_moves) {
                                if(sb->hstack[sb->hply - 1].checks)
                                    sb->pstack->best_score = -MATE_SCORE + WIN_PLY * (sb->ply + 1);
                                else 
                                    sb->pstack->best_score = 0;
                                sb->pstack->flag = EXACT;
                            }  
#ifdef PARALLEL
                            else if((use_abdada_smp == 1)
                                && !sb->pstack->all_done
                                && !sb->pstack->second_pass
                                ) {
                                    sb->pstack->second_pass = true;
                                    sb->pstack->all_done = true;
                                    sb->pstack->gen_status = GEN_RESET;
                                    continue;
                            }
#endif
                            GOBACK(true);
                        } else {
#ifdef PARALLEL
                            if((use_abdada_smp == 1)
                                && !sb->pstack->all_done
                                && sb->pstack->second_pass
                                && sb->pstack->score_st[sb->pstack->current_index - 1] != -SKIP_SCORE
                                ) {
                                    sb->pstack->legal_moves++;
                                    continue;
			    }
#endif
                        }

                        sb->pstack->legal_moves++;
                    }
                    /*save start nodes count*/
                    sb->pstack->start_nodes = sb->nodes;
                    /*
                    * singular search?
                    */
                    if(sb->pstack->legal_moves == 1
                        && (sb->pstack->search_state & ~MOVE_MASK) == SINGULAR_SEARCH) {
                            continue;
                    }
                    /*
                    * play the move
                    */
                    sb->PUSH_MOVE(sb->pstack->current_move);
                    sb->nodes++;

                    /*set next ply's depth and be selective*/           
                    sb->pstack->depth = (sb->pstack - 1)->depth - UNITDEPTH;
                    if(use_selective && sb->be_selective((sb->pstack - 1)->legal_moves,false)) {
                        sb->POP_MOVE();
                        continue;
                    }
                    /*next ply's window*/
                    if(use_pvs && (sb->pstack - 1)->node_type == PV_NODE && (sb->pstack - 1)->legal_moves > 1) {
                        sb->pstack->alpha = -(sb->pstack - 1)->alpha - 1;
                        sb->pstack->beta = -(sb->pstack - 1)->alpha;
                        sb->pstack->node_type = CUT_NODE;
                        sb->pstack->search_state = NULL_MOVE;
                    } else {
                        sb->pstack->alpha = -(sb->pstack - 1)->beta;
                        sb->pstack->beta = -(sb->pstack - 1)->alpha;
                        if((sb->pstack - 1)->legal_moves > 3)
                            sb->pstack->node_type = CUT_NODE;
                        else
                            sb->pstack->node_type = (sb->pstack - 1)->next_node_type;
                        sb->pstack->search_state = NULL_MOVE;
                    }

                    /*go to new node*/
                    goto NEW_NODE;
                }
            }
            /*
            * At this point we have made a normal/null move and already set up
            * the bounds,depth and other next node states. It is time to check all
            * the conditions which cause cutoff (repetition draws, hashtable/bitbase cutoffs,
            * qsearch(Yes:). Qsearch is implemented separately.
            */
NEW_NODE:
            if( (!sb->finish_search &&
                (sb->stop_searcher || sb->abort_search) ) ||
                sb->on_node_entry() )
                goto POP;
        }
POP:
        /*
        * Terminate search on current block if stop signal is on
        * or if we finished searching.
        */
        if( (!sb->finish_search &&
            (sb->stop_searcher || sb->abort_search) ) ||
            sb->stop_ply == sb->ply
            ) {
#ifndef PARALLEL
                return;
#else
                /*exit here on single processor search*/
                if(single)
                    return;
                /*
                * Is this processor a slave?
                */
                if(sb->master) {
                    l_lock(lock_smp);
                    l_lock(sb->master->lock);
                    sb->update_master(use_abdada_smp);
                    active_workers = sb->master->n_workers;
                    CLUSTER_CODE(active_hosts = sb->master->n_host_workers);
                    l_unlock(sb->master->lock);
                    l_unlock(lock_smp);
#ifdef CLUSTER
                    /* Wait until last helper host finishes*/ 
                    if(!active_workers && active_hosts) {
                        while(sb->master->n_host_workers) {
                            proc->idle_loop();
                            t_yield();
                        }
                        active_hosts = 0;
                    }
#endif
                    /*
                    * We have run out of work now. If this was the _last worker_, grab master 
                    * search block and continue searching with it (i.e. backup). Otherwise goto 
                    * idle mode until work is assigned. In a recursive search, only the thread 
                    * that initiated the split can backup.
                    */
                    if(!use_abdada_smp
                        && !active_workers 
                        CLUSTER_CODE( && !active_hosts)
                        ) {
                            l_lock(lock_smp);
                            SEARCHER* sbm = sb->master;
                            /* switch worker processors*/
                            if(sbm->processor_id != sb->processor_id && sbm->master) {
                                sbm->master->workers[sbm->processor_id] = 0; 
                                sbm->master->workers[sb->processor_id] = sbm;
                            }
                            sbm->processor_id = sb->processor_id;
                            /*end*/
                            sbm->stop_searcher = 0;
                            sb->used = false;
                            sb = sbm;
                            proc->searcher = sbm;
                            /*unlock*/
                            l_unlock(lock_smp);

                            GOBACK(true);
                    } else {
                        /*
                        * make processor idle
                        */ 
                        l_lock(lock_smp);
                        sb->used = false;
                        if(proc->state == GO) {
                            proc->searcher = NULL;
                            proc->state = WAIT;
                        }
                        l_unlock(lock_smp);
IDLE_START: 
                        /*
                        * processor's state loop
                        */
                        if(proc->state <= WAIT) {
                            l_lock(lock_smp);       
                            PROCESSOR::n_idle_processors++;
                            PROCESSOR::set_num_searchers();
                            l_unlock(lock_smp);

                            proc->idle_loop();

                            l_lock(lock_smp);       
                            PROCESSOR::n_idle_processors--; 
                            PROCESSOR::set_num_searchers();
                            l_unlock(lock_smp);
                        }
                        if(proc->state == GO || proc->state == GOSP) {
                            sb = proc->searcher;
                            if(montecarlo || proc->state == GOSP) {
                                if(proc->state == GOSP) {
                                    proc->state = GO;
                                    sb->worker_thread();
                                } else
                                    sb->search_mc();

                                l_lock(lock_smp);
                                sb->used = false;
                                proc->searcher = NULL;
                                proc->state = WAIT;
                                l_unlock(lock_smp);

                                goto IDLE_START;
                            } else {
                                if(!use_abdada_smp) 
                                    goto START;
                                else 
                                    goto NEW_NODE;
                            }
                        } else if(proc->state == KILL) {
                            proc->state = GO;
                            return;
                        }
                    }
                } else {
                    /*
                    * Only processor[0] can return from here to the root! If some other 
                    * processor reached here first, switch to processor[0] and return from there. 
                    * Also send the current processor to sleep.
                    */
                    if(sb->processor_id == 0) {
                        return;
                    } else {
                        l_lock(lock_smp);
                        if(!use_abdada_smp) {
                            sb->processor_id = 0;
                            processors[0]->searcher = sb;
                            processors[0]->state = KILL;
                        } else {
                            sb->used = false;
                        }
                        proc->searcher = NULL;
                        proc->state = WAIT;
                        l_unlock(lock_smp);
                        goto IDLE_START;
                    }
                }
#endif
        }
        /*
        * decide to go back one step OR research
        * with a different window/depth
        */
        score = -sb->pstack->best_score;

        switch((sb->pstack - 1)->search_state & MOVE_MASK) {
        case NULL_MOVE:
            sb->POP_NULL();
            break;
        case NORMAL_MOVE:
#ifdef PARALLEL
            /*remeber skipped moves*/
            if((use_abdada_smp == 1)
                && score == -SKIP_SCORE
                ) {
                sb->POP_MOVE();
                sb->pstack->all_done = false;
                sb->pstack->score_st[sb->pstack->current_index - 1] = score;
                continue;
            }
#endif
            /*research with full depth*/
            if(sb->pstack->reduction > 0
                && score > (sb->pstack - 1)->alpha
                ) {
                    sb->pstack->depth += UNITDEPTH;
                    sb->pstack->reduction -= UNITDEPTH;
                    sb->pstack->alpha = -(sb->pstack - 1)->alpha - 1;
                    sb->pstack->beta = -(sb->pstack - 1)->alpha;
                    sb->pstack->node_type = CUT_NODE;
                    sb->pstack->search_state = NULL_MOVE;
                    goto NEW_NODE;
            }
            /*research with full window*/
            if((sb->pstack - 1)->node_type == PV_NODE 
                && sb->pstack->node_type != PV_NODE
                && score > (sb->pstack - 1)->alpha
                && score < (sb->pstack - 1)->beta
                ) {
                    sb->pstack->alpha = -(sb->pstack - 1)->beta;
                    sb->pstack->beta = -(sb->pstack - 1)->alpha;
                    sb->pstack->node_type = PV_NODE;
                    sb->pstack->search_state = NULL_MOVE;
                    goto NEW_NODE;
            }
            sb->POP_MOVE();
            break;
        }
        /*
        * At this point move is taken back after researches,if any,are done.
        * So we can update alpha now.
        */
        switch(sb->pstack->search_state & MOVE_MASK) {
        case NULL_MOVE:
            if(score >= sb->pstack->beta) { 
                sb->pstack->best_score = score;
                sb->pstack->best_move = 0;
                sb->pstack->flag = LOWER;
                GOBACK(true);
            } else {
                if(score == -MATE_SCORE + WIN_PLY * (sb->ply + 3))
                    sb->pstack->mate_threat = 1;
            }
            sb->pstack->search_state = PROBCUT_SEARCH;
            break;
        case NORMAL_MOVE:
            move = sb->pstack->current_move;
            /*update best move at root and non-root nodes differently*/
            if(!sb->ply && !single) {

                l_lock(lock_smp);
                sb->root_score_st[sb->pstack->current_index - 1] 
                      += (sb->nodes - sb->pstack->start_nodes);
                l_unlock(lock_smp);

                if(sb->pstack->current_index == 1 || score > sb->pstack->alpha) {
                    sb->pstack->best_score = score;
                    sb->pstack->best_move = move;

                    if(use_abdada_smp) {
                        if(score >= sb->pstack->beta) (sb->pstack + 1)->pv_length = 1;
                        sb->UPDATE_PV(move);
                    } else {
                        if(score >= sb->pstack->beta) {
                            if(sb->pstack->current_index == 1)
                                sb->pstack->pv_length = prev_pv_length;
                            else {
                                (sb->pstack+1)->pv_length = 1;
                                sb->UPDATE_PV(move);
                                prev_pv_length = 1;
                            }
                        } else if(score <= sb->pstack->alpha) {
                            sb->pstack->pv_length = prev_pv_length;
                        } else {
                            sb->UPDATE_PV(move);
                        }
                    }

                    if(score <= sb->pstack->alpha || score >= sb->pstack->beta);
                    else SMP_CODE(if(!use_abdada_smp || !sb->processor_id))
                        sb->print_pv(sb->pstack->best_score);

                    if(!sb->chess_clock.infinite_mode && !sb->chess_clock.pondering)
                        sb->root_score = score;

                    if(score <= sb->pstack->alpha) {
                        SEARCHER::root_failed_low = 3;
                        GOBACK(true);
                    }
                }
            } else if(score > sb->pstack->best_score) {
                sb->pstack->best_score = score;
                sb->pstack->best_move = move;
            }
            /*update bounds*/
            if(score > sb->pstack->alpha) {
                if(score >= sb->pstack->beta) {
                    sb->pstack->flag = LOWER;
                    if(!is_cap_prom(move))
                        sb->update_history(move);
#ifdef PARALLEL     
                    /* stop workers*/
                    if(!use_abdada_smp && !single && 
                        sb->master && sb->stop_ply == sb->ply)
                        sb->master->handle_fail_high();
#endif
                    GOBACK(true);
                }
                sb->pstack->alpha = score;
                sb->pstack->flag = EXACT;
                sb->UPDATE_PV(move);
            }
#ifdef PARALLEL 
            /* Check for split here since at least one move is searched now.
             * Reset the sb pointer to the child block pointed to by this processor.*/
            if(!use_abdada_smp && !single
                && (sb->ply || sb->pstack->legal_moves >= 4) 
                && sb->check_split())
                sb = proc->searcher;
#endif
            break;
        }
        /* Go up and process another move.
         */
        continue;

SPECIAL:
        switch(sb->pstack->search_state & ~MOVE_MASK) {
        case PROBCUT_SEARCH:
            if(!sb->pstack->hash_move)
                sb->pstack->hash_move = sb->pstack->best_move;
            if((sb->pstack->flag == LOWER && sb->pstack->node_type == CUT_NODE) ||
               (sb->pstack->flag == UPPER && sb->pstack->node_type == ALL_NODE) )
                goto POP;
            sb->pstack->search_state = IID_SEARCH;
            sb->pstack->gen_status = GEN_START;
            break;
        case IID_SEARCH:
            sb->pstack->hash_move  = sb->pstack->best_move;
            sb->pstack->hash_score = sb->pstack->best_score;
            sb->pstack->hash_depth = sb->pstack->depth;
            sb->pstack->hash_flags = sb->pstack->flag;
            if(sb->pstack->flag == EXACT || sb->pstack->flag == LOWER)
                sb->pstack->hash_flags = HASH_GOOD;
            sb->pstack->search_state = SINGULAR_SEARCH;
            sb->pstack->gen_status = GEN_START;
            break;
        case SINGULAR_SEARCH:
            if(sb->pstack->flag == UPPER)
                sb->pstack->singular = 1;
            sb->pstack->search_state = NORMAL_MOVE;
            sb->pstack->gen_status = GEN_START;
            break;
        } 
        sb->pstack->alpha = sb->pstack->o_alpha;
        sb->pstack->beta = sb->pstack->o_beta;
        sb->pstack->depth = sb->pstack->o_depth;
        sb->pstack->flag = UPPER;
        sb->pstack->legal_moves = 0;
        sb->pstack->best_score = -MATE_SCORE;
        sb->pstack->best_move = 0;
        sb->pstack->pv_length = sb->ply;
        sb->pstack->all_done = true;
        sb->pstack->second_pass = false;
    }
}
/*
Back up to previous ply
*/
#define GOBACK_Q(save) {                                            \
    if(use_tt && save && !abort_search && !stop_searcher) {         \
        int depth = 0;                                              \
        if( (hply >= 1 && hstack[hply - 1].checks) ||               \
             pstack->qcheck_depth > 0) depth = UNITDEPTH;           \
        RECORD_HASH(player,hash_key,depth,ply,pstack->flag,         \
                    pstack->best_score,pstack->best_move,0,0);      \
    }                                                               \
    goto POP_Q;                                                     \
};
/*
quiescent search
*/
void SEARCHER::qsearch() {

    int score;
    int stop_ply = ply;

    goto NEW_NODE_Q;

    while(true) {
        while(true) {

            /*get legal move*/
            if(!get_qmove()) {
                if(hply >= 1 && hstack[hply - 1].checks && pstack->legal_moves == 0)
                    pstack->best_score = -MATE_SCORE + WIN_PLY * (ply + 1);
                GOBACK_Q(true);
            }

            pstack->legal_moves++;

            PUSH_MOVE(pstack->current_move);
            nodes++;
            qnodes++;

            /*next ply's window and depth*/ 
            pstack->alpha = -(pstack - 1)->beta;
            pstack->beta = -(pstack - 1)->alpha;
            pstack->depth = (pstack - 1)->depth - UNITDEPTH;
            pstack->node_type = (pstack - 1)->next_node_type;
            pstack->qcheck_depth = (pstack - 1)->qcheck_depth - UNITDEPTH;
            /*end*/

NEW_NODE_Q:     
            /*
            NEW node entry point
            */
            if( (!finish_search &&
                (stop_searcher || abort_search) ) ||
                on_qnode_entry() )
                goto POP_Q;
        }
POP_Q:
        if( (!finish_search &&
            (stop_searcher || abort_search) ) ||
            stop_ply == ply
            ) {
            return;
        }

        POP_MOVE();
        score = -(pstack + 1)->best_score;
        
        if(score > pstack->best_score) {
            pstack->best_score = score;
            pstack->best_move = pstack->current_move;

            if(score > pstack->alpha) {
                if(score >= pstack->beta) {
                    pstack->flag = LOWER;
                    GOBACK_Q(true);
                }
                pstack->alpha = score;
                pstack->flag = EXACT;
                UPDATE_PV(pstack->current_move);
            }
        }
    }
}

void SEARCHER::qsearch_nn() {
    if(use_nn && !skip_nn) {
        int sc1, sc2;

        bool save = skip_nn;
        skip_nn = true;
        qsearch();
        sc1 = eval();
        skip_nn = save;
        sc2 = eval();

        pstack->best_score = sc2 + (pstack->best_score - sc1);
    } else {
        qsearch();
    }
}
/*
searcher's search function
*/
void SEARCHER::idle_loop_main() {
#ifdef PARALLEL
    l_lock(lock_smp);       
    PROCESSOR::n_idle_processors++;
    PROCESSOR::set_num_searchers();
    l_unlock(lock_smp);

    while(PROCESSOR::n_idle_processors < PROCESSOR::n_processors) {
        t_yield();
        if(SEARCHER::use_nn) t_sleep(SEARCHER::delay);
    }
    
    l_lock(lock_smp);       
    PROCESSOR::n_idle_processors--;
    PROCESSOR::set_num_searchers();
    l_unlock(lock_smp);
#endif
}

void SEARCHER::search() {

    /*mcts*/
    if(montecarlo) {
#ifdef PARALLEL
            /*attach helper processor here once*/
            l_lock(lock_smp);
            for(int i = 1;i < PROCESSOR::n_processors;i++) {
                if(processors[i]->state == WAIT) {
                    attach_processor(i);
                    processors[i]->state = GO;
                }
            }
            l_unlock(lock_smp);

            /*montecarlo search*/
            search_mc();

            /*wait till all helpers become idle*/
            stop_workers();
            idle_loop_main();
#else
            search_mc();
#endif

    /*alphabeta*/
    } else {

#ifdef PARALLEL
        /*attach helper processor here once for abdada*/
        if(use_abdada_smp) {
            l_lock(lock_smp);
            for(int i = 1;i < PROCESSOR::n_processors;i++) {
                if(processors[i]->state == WAIT) {
                    attach_processor(i);
                    PSEARCHER sb = processors[i]->searcher;
                    memcpy(&sb->pstack->move_st[0],&pstack->move_st[0], 
                        pstack->count * sizeof(MOVE));
                    processors[i]->state = GO;
                }
            }
            l_unlock(lock_smp);
        }

        /*do the search*/
        ::search(processors[0]);

        /*wait till all helpers become idle*/
        if(use_abdada_smp) {
            stop_workers();
            idle_loop_main();
        }
#else
        ::search(this);
#endif
    }
}
/*
Get search score
*/
int SEARCHER::search_ab() {
    if(pstack->depth <= 0) qsearch_calls++;
    else search_calls++;
#ifdef PARALLEL
    ::search(processors[processor_id],true);
#else
    ::search(this,true);
#endif
    return pstack->best_score;
}
/*
Evaluate moves with search
*/
void SEARCHER::evaluate_moves(int depth, int alpha, int beta) {
    int score = 0, WINDOW = 2*aspiration_window, bscore = -MATE_SCORE;

    finish_search = true;

    for(int i = 0;i < pstack->count; i++) {
        pstack->current_move = pstack->move_st[i];
        PUSH_MOVE(pstack->current_move);
        nodes++;
        if(depth <= 0) qnodes++;
        alpha = -MATE_SCORE;
        beta = MATE_SCORE;
        
        if(egbb_is_loaded && all_man_c <= MAX_EGBB 
            && bitbase_cutoff()
            ) {
            score = -pstack->best_score;
            pstack->pv_length = ply;
        } else {
            int newd = depth;
            for(int j = 0;j < 8;j++) {
                if(i >= lmr_count[j] && newd > 1) {
                    newd--;
                }
            }
            score = (pstack-1)->score_st[i];
            if(ply == 1 && newd >= 3) {
                alpha = score - WINDOW;
                beta  = score + WINDOW;
            }
TOP:
            pstack->alpha = -beta;
            pstack->beta = -alpha;
            pstack->depth = newd;
            pstack->node_type = PV_NODE;
            pstack->search_state = NORMAL_MOVE;
            pstack->extension = 0;
            pstack->reduction = 0;
            pstack->pv_length = ply;
            pstack->best_score = -MATE_SCORE;

            if(depth == -4) {
                score = -eval();
            } else if(depth == -3) {
                MOVE& move = (pstack-1)->current_move;
                score = -eval() + 
                  (see(move) - piece_see_v[m_capture(move)]);
            } else {
                if(depth < 0) qsearch_level = depth;
                search_ab();
                if(depth < 0) qsearch_level = 0;
                score = -pstack->best_score;
            }

            if(!abort_search &&
                ply == 1 && newd >= 3
                ) {
                if(score <= alpha) {
                    WINDOW = MIN(200, 3 * WINDOW / 2);
                    alpha = MAX(-MATE_SCORE,score - WINDOW);
                    goto TOP;
                } else if(score >= beta) {
                    WINDOW = MIN(200, 3 * WINDOW / 2);
                    beta = MIN(MATE_SCORE,score + WINDOW);
                    goto TOP;
                }
            }

        }
        POP_MOVE();

        if(!ply && abort_search)
            break;

        if(!ply && depth >= 10 && score > bscore) {
            UPDATE_PV(pstack->current_move);
            print_pv(score);
            bscore = score;
        }

        pstack->score_st[i] = score;
    }

    finish_search = false;

    for(int i = 0;i < pstack->count; i++)
        pstack->sort(i,pstack->count);

}
/*
Find best move using alpha-beta or mcts
*/
MOVE SEARCHER::iterative_deepening() {
    int score;
    int easy = false,easy_score = 0;
    MOVE easy_move = 0;

    search_depth = 1;
#ifdef CLUSTER
    if(PROCESSOR::n_hosts > 1 && use_abdada_cluster) 
        search_depth = 1 - (PROCESSOR::host_id & 1);
#endif
    poll_nodes = 5000;
    PROCESSOR::age = (hply & AGE_MASK);
    clear_history();
    show_full_pv = false;
    freeze_tree = false;

    /*iterative deepening*/
    int alpha,beta,WINDOW = 2*aspiration_window;
    int prev_depth = search_depth;
    int prev_root_score = 0;
    alpha = -MATE_SCORE; 
    beta = MATE_SCORE;
    root_failed_low = 0;
    time_factor = 1.0;
    pstack->node_type = PV_NODE;
    pstack->search_state = NORMAL_MOVE;
    pstack->extension = 0;
    pstack->reduction = 0;
    pstack->all_done = true;
    pstack->second_pass = false;

    /*easy move*/
    if(!montecarlo && !use_nn
        && pstack->score_st[0] > pstack->score_st[1] + 175
        && chess_clock.is_timed()
        ) {
        easy = true;
        easy_move = pstack->move_st[0];
        easy_score = pstack->score_st[0];
        chess_clock.search_time /= 4;
    }

    /*AB prior search*/
    if(montecarlo) {
        
        has_ab = (frac_abprior > 0) &&
            !is_selfplay && (chess_clock.max_visits == MAX_NUMBER) &&
            (all_man_c <= alphabeta_man_c || root_score >= 400);

        manage_tree();
        Node::max_tree_depth = 0;

        /*Take out so far spent time in the last move*/
        if(chess_clock.maximum_time <= 2 * chess_clock.search_time)
            chess_clock.p_time -= (get_time() - start_time);

        /*Alpha-beta prior search */
        if(has_ab) {

#ifdef PARALLEL
        for(int i = PROCESSOR::n_cores;i < PROCESSOR::n_processors;i++)
            processors[i]->state = PARK;
#endif

#ifdef NODES_PRIOR
            /*do ab search*/
            montecarlo = 0;
            use_nn = 0;

            chess_clock.p_time *= frac_abprior;
            chess_clock.p_inc *= frac_abprior;

            iterative_deepening();
#ifdef PARALLEL
            t_sleep(30);
#endif
            chess_clock.p_time /= frac_abprior;
            chess_clock.p_inc /= frac_abprior;

            montecarlo = 1;
            use_nn = save_use_nn;

            while(ply > 0) {
                if(hstack[hply - 1].move) POP_MOVE();
                else POP_NULL();
            }

            /*nodes to prior*/
            uint64_t maxn = 0;
            int maxni = 0;
            for(int i = 1; i < pstack->count; i++) {
                uint64_t v = root_score_st[i];
                if(v > maxn) { maxn = v; maxni = i; }
            }

            if(root_score_st[0] < 1.5 * root_score_st[maxni])
                root_score_st[0] = 1.5 * root_score_st[maxni];

            /*assign prior*/
            Node* current = root_node->child;
            while(current) {
                for(int i = 0;i < pstack->count; i++) {
                    MOVE& move = pstack->move_st[i];
                    if(move == current->move) {
                        double v = sqrt(double(root_score_st[i]) / maxn);
                        current->prior = -(v - 0.5) * 100;
                        if(rollout_type == MCTS)
                            current->prior = logistic(current->prior);
                        current->rank = i + 1;
#if 0
                        char mvs[16];
                        mov_str(move,mvs);
                        print("%2d. %6s %9.6f %9.6f " FMT64W " " FMT64W "\n", 
                            current->rank, mvs, current->prior,
                            v, root_score_st[i], maxn);
#endif
                        break;
                    }
                }
                current = current->next;
            }
#else
            /*do ab search*/
            montecarlo = 0;
            use_nn = 0;

            chess_clock.p_time *= frac_abprior;
            chess_clock.p_inc *= frac_abprior;
            chess_clock.set_stime(hply,false);
            chess_clock.p_time /= frac_abprior;
            chess_clock.p_inc /= frac_abprior;

            start_time = get_time();

            for(int d = 2; d < MAX_PLY - 1 ; d++) {
                stop_searcher = 0;
                search_depth = d;
                evaluate_moves(d,-MATE_SCORE,MATE_SCORE);

                int time_used = get_time() - start_time;
                if(abort_search || time_used >= 0.75 * chess_clock.search_time)
                    break;
                if(!easy 
                    && pstack->score_st[0] > pstack->score_st[1] + 175
                    && chess_clock.is_timed()
                    ) {
                    easy = true;
                    easy_move = pstack->move_st[0];
                    easy_score = pstack->score_st[0];
                } else if(easy && 
                    (easy_move != pstack->pv[0] || pstack->score_st[0] <= easy_score - 60)) {
                    easy = false;
                }
            }

            use_nn = save_use_nn;
            montecarlo = 1;

            while(ply > 0) {
                if(hstack[hply - 1].move) POP_MOVE();
                else POP_NULL();
            }

            /*assign prior*/
            Node* current = root_node->child;
            while(current) {
                for(int i = 0;i < pstack->count; i++) {
                    MOVE& move = pstack->move_st[i];
                    if(move == current->move) {
                        current->prior = -pstack->score_st[i];
                        if(rollout_type == MCTS)
                            current->prior = logistic(current->prior);
                        current->rank = i + 1;
                        break;
                    }
                }
                current = current->next;
            }
#endif

            /* No further search in these cases */
            if(root_score >= 700 ||
                search_depth >= MAX_PLY - 4 ||
                frac_abprior >= 0.95
                )
                return stack[0].pv[0];

            /*reset flags for mcts*/
            stop_searcher = 0;
            abort_search = 0;
            search_depth = MIN(search_depth + mcts_strategy_depth, MAX_PLY - 2);
            
            /* wake mcts threads*/
#ifdef PARALLEL
            for(int i = PROCESSOR::n_cores;i < PROCESSOR::n_processors;i++)
                processors[i]->state = WAIT;
#endif
        }

        /*rank nodes and reset bounds*/
        if(rollout_type == ALPHABETA) {
            root_node->alpha = alpha;
            root_node->beta = beta;
            Node::parallel_job(root_node,rank_reset_thread_proc,true);
        }
    }

    /*set search time and record start time*/
    if(!chess_clock.infinite_mode) {
        if(montecarlo && has_ab) {
            chess_clock.p_time *= (1 - frac_abprior);
            chess_clock.p_inc *= (1 - frac_abprior);
            chess_clock.set_stime(hply,false);
            chess_clock.p_time /= (1 - frac_abprior);
            chess_clock.p_inc /= (1 - frac_abprior);
        } else {
            chess_clock.set_stime(hply,false);
        }
        if(easy) chess_clock.search_time /= 4;

        /*reduce time according to root node visits*/
        if(montecarlo && rollout_type == MCTS &&
            average_pps > 0) {
            int time_red = root_node->visits / (average_pps / 1000.0f);
            if(time_red > 0.5 * chess_clock.search_time)
                time_red = 0.5 * chess_clock.search_time;
            if(pv_print_style == 0)
                print_info("Reducing time by %d ms to %dms\n",
                    time_red, chess_clock.search_time - time_red);
            chess_clock.search_time -= time_red;
        }
    }

    start_time = get_time();
   
    /*iterative deepening*/
    while(search_depth < chess_clock.max_sd) {

        /*search with the current depth*/
        search_depth++;
#ifdef CLUSTER
        if(use_abdada_cluster && PROCESSOR::n_hosts > 1 && 
           search_depth < chess_clock.max_sd) 
            search_depth++;
#endif
        /*egbb ply limit*/
        if(!montecarlo)
            SEARCHER::egbb_ply_limit = 
                SEARCHER::egbb_ply_limit_percent * search_depth / 100;
        else
            SEARCHER::egbb_ply_limit = 8;

        /*Set bounds and search.*/
        pstack->depth = search_depth * UNITDEPTH;
        pstack->alpha = alpha;
        pstack->beta = beta;

        /*search*/
        search();
        
        /*check if search is aborted*/
        if(abort_search)
            break;

        /*best score*/
        score = pstack->best_score;

        /*fail low at root*/
        if(!montecarlo && root_failed_low && score > alpha) {
            root_failed_low--;
            if(root_failed_low && prev_root_score - root_score < 50) 
                root_failed_low--;
            if(root_failed_low && prev_root_score - root_score < 25) 
                root_failed_low--;
        }

        /*check if "easy" move is really easy*/
        if(easy && (easy_move != pstack->pv[0] || score <= easy_score - 60)) {
            easy = false;
            chess_clock.search_time *= 4;
        }
        
#if 0
        if(score <= alpha) print("--");
        else if(score >= beta) print("++");
        else print("==");
        print(" %d [%d %d] pvlength %d\n",score,alpha,beta,stack[0].pv_length);
        
        if(montecarlo) {
            for(int r = 1; r <= MAX_MOVES; r++) {
                Node* current = root_node->child;
                while(current) {
                    if(current->rank == r) {
                        print("%d. ",current->rank);
                        print_move(current->move);
                        print(" score %d visits %d bounds %d %d \n",
                            int(-current->score),current->visits,
                            -current->beta, -current->alpha);
                        break;
                    }
                    current = current->next;
                }
            }
        }
#endif
        
        /*aspiration search*/
        if(!use_aspiration || 
            ABS(score) >= 1000 || 
            search_depth <= 3
            ) {
            alpha = -MATE_SCORE;
            beta = MATE_SCORE;
            prev_pv_length = stack[0].pv_length;
        } else if(score <= alpha) {
            WINDOW = MIN(200, 3 * WINDOW / 2);
            alpha = MAX(-MATE_SCORE,score - WINDOW);
            search_depth--;
#ifdef CLUSTER
            if(use_abdada_cluster && PROCESSOR::n_hosts > 1) 
                search_depth--;
#endif
        } else if (score >= beta){
            WINDOW = MIN(200, 3 * WINDOW / 2);
            beta = MIN(MATE_SCORE,score + WINDOW);
            search_depth--;
#ifdef CLUSTER
            if(use_abdada_cluster && PROCESSOR::n_hosts > 1) 
                search_depth--;
#endif
        } else {
            WINDOW = aspiration_window;
            alpha = score - WINDOW;
            beta = score + WINDOW;
            prev_pv_length = stack[0].pv_length;
        }

        /*sort moves*/
        if(!montecarlo) {
            /* Is there enough time to search the first move?
             * First move taking lot more time than second. */
            if(!root_failed_low && chess_clock.is_timed()) {
                int time_used = get_time() - start_time;
                double ratio = double(root_score_st[0]) / root_score_st[1];
                if((time_used >= 0.75 * chess_clock.search_time) || 
                   (time_used >= 0.5 * chess_clock.search_time && ratio >= 2.0)  ) {
                    abort_search = 1;
                    break;
                }
            }

            /*install fake pv into TT table so that it is searched
            first incase it was overwritten*/
            if(pstack->pv_length) {
                for(int i = 0;i < stack[0].pv_length;i++) {
                    RECORD_HASH(player,hash_key,0,0,HASH_HIT,0,stack[0].pv[i],0,0);
                    PUSH_MOVE(stack[0].pv[i]);
                }
                for(int i = 0;i < stack[0].pv_length;i++)
                    POP_MOVE();
            }

            /*sort moves*/
            MOVE tempm;
            uint64_t temps,bests = 0;

            for(int i = 0;i < pstack->count; i++) {
                if(pstack->pv[0] == pstack->move_st[i]) {
                    bests = root_score_st[i];
                    root_score_st[i] = MAX_UBMP64;
                }
            }
            for(int i = 0;i < pstack->count; i++) {
                for(int j = i + 1;j < pstack->count;j++) {
                    if(root_score_st[i] < root_score_st[j]) {
                        tempm = pstack->move_st[i];
                        temps = root_score_st[i];
                        pstack->move_st[i] = pstack->move_st[j];
                        root_score_st[i] = root_score_st[j];
                        pstack->move_st[j] = tempm;
                        root_score_st[j] = temps;
                    }
                }
            }
            for(int i = 0;i < pstack->count; i++) {
                if(pstack->pv[0] == pstack->move_st[i]) {
                    root_score_st[i] = bests;
                }
            }
        } else {
            /* Is there enough time to search the first move?*/
            if(rollout_type == ALPHABETA) {
                if(!root_failed_low && chess_clock.is_timed()) {
                    int time_used = get_time() - start_time;
                    if(time_used >= 0.75 * chess_clock.search_time) {
                        abort_search = 1;
                        break;
                    }
                }
            }
            /*rank nodes and reset bounds*/
            if(rollout_type == ALPHABETA) {
                /*Switching rollouts type*/
                double frac = double(get_time() - start_time) / chess_clock.search_time;
                if(frac_alphabeta != 1.0 && frac > frac_alphabeta) {
                    print_info("Switching rollout type to MCTS.\n");
                    Node::parallel_job(root_node,convert_score_thread_proc);
                    rollout_type = MCTS;
                    use_nn = save_use_nn;
                    search_depth = MIN(search_depth + mcts_strategy_depth, MAX_PLY - 2);
                    pstack->depth = search_depth * UNITDEPTH;
                    root_failed_low = 0;
                    freeze_tree = false;
                }
                /*alphabeta bounds*/
                root_node->alpha = alpha;
                root_node->beta = beta;
                Node::parallel_job(root_node,rank_reset_thread_proc,true);
            }
        }

        /*check time*/
        check_quit();

        /*store info*/
        if(search_depth > prev_depth) {
            prev_root_score = root_score;
            prev_depth = search_depth;
        }
        /*end*/
    }

    /*search info*/
    if(montecarlo) {

        /*search has ended. display some info*/
        int time_used = MAX(1,get_time() - start_time);
        int time_used_o = MAX(1,get_time() - start_time_o);
        unsigned int pps = int(playouts / (time_used / 1000.0f));
        unsigned int vps = int(root_node->visits / (time_used / 1000.0f));
        if(average_pps > 0 && pps >= 5 * average_pps);
        else average_pps = pps;

        if(pv_print_style == 1) {
            print(" %21d %8.2f %10d %10d\n",
                root_node->visits,float(time_used) / 1000,pps,
                int(int64_t(nnecalls) / (time_used / 1000.0f)));
        } else if(pv_print_style == 0) {
            /*print tree*/
            if(multipv)
                Node::print_tree(root_node,has_ab,MAX_PLY);

            /* print result*/
            print_info("Stat: nodes " FMT64 " <%d%% qnodes> tbhits %d qcalls %d scalls %d time %dms nps %d eps %d\n",nodes,
                int(int64_t(qnodes) / (int64_t(nodes) / 100.0f)),
                egbb_probes,qsearch_calls,search_calls,
                time_used_o,int(int64_t(nodes) / (time_used_o / 1000.0f)),
                int(int64_t(ecalls) / (time_used_o / 1000.0f)));
            print_info("Tree: nodes %d depth %d visits %d Rate: vps %d pps %d nneps %d\n",
                  Node::total_nodes,Node::max_tree_depth,root_node->visits,vps,pps,
                   int(int64_t(nnecalls) / (time_used / 1000.0f)));
        }
        
    } else {

#ifdef CLUSTER
        /*total nps*/
        if(use_abdada_cluster) {
            uint64_t tnodes;
            PROCESSOR::Sum(&nodes, &tnodes);
            nodes = (uint64_t)tnodes;
        }
#endif

    	/*print final pv*/
    	for (int j = ply; j > 0 ; j--) {
                MOVE move = hstack[hply - 1].move;
                if(move) POP_MOVE();
                else POP_NULL();
            }
    	if(!pstack->pv_length)
    	    pstack->pv_length = 1;
    	print_pv(root_score);

        /*search has ended. display some info*/
        int time_used = get_time() - start_time;
        if(!time_used) time_used = 1;
        if(pv_print_style == 1) {
            print(" " FMT64W " %8.2f %10d %8d %8d\n",nodes,float(time_used) / 1000,
                int(int64_t(nodes) / (time_used / 1000.0f)),splits,bad_splits);
        } else if(pv_print_style == 0) {
            print_info("Stat: nodes " FMT64 " <%d%% qnodes> tbhits %d splits %d badsplits %d time %dms nps %d eps %d nneps %d\n",nodes,
                int(int64_t(qnodes) / (int64_t(nodes) / 100.0f)),
                egbb_probes,splits,bad_splits,
                time_used,int(int64_t(nodes) / (time_used / 1000.0f)),
                int(int64_t(ecalls) / (time_used / 1000.0f)),
                int(int64_t(nnecalls) / (time_used / 1000.0f)));
        }

    }

    return stack[0].pv[0];
}
/*
* Find best move using alpha-beta or mcts
*/
MOVE SEARCHER::find_best() {

    start_time = start_time_o = get_time();

    /*
    send initial position to helper hosts
    */
#ifdef CLUSTER
    if(PROCESSOR::host_id == 0) {
        INIT_MESSAGE init;
        get_init_pos(&init);
        for(int i = 0;i < PROCESSOR::n_hosts;i++) {
            if(i != PROCESSOR::host_id) {
                PROCESSOR::ISend(i,PROCESSOR::INIT,&init,INIT_MESSAGE_SIZE(init));
                /*start iterative deepening searcher for ABDADA/SHT*/
                if(use_abdada_cluster || montecarlo)
                    PROCESSOR::ISend(i,PROCESSOR::GOROOT);
            }
        }
    }
#endif

    /*init*/
    ply = 0;
    pstack = stack + 0;
    stop_searcher = 0;
    abort_search = 0;
    time_check = 0;
    message_check = 0;
    nodes = 0;
    qnodes = 0;
    ecalls = 0;
    playouts = 0;
    nnecalls = 0;
    splits = 0;
    bad_splits = 0;
    search_calls = 0;
    qsearch_calls = 0;
    egbb_probes = 0;
    prev_pv_length = 0;
    old_root_score = root_score;
#ifdef PARALLEL
    PROCESSOR::set_num_searchers();
#endif

    /*fen*/
    if(pv_print_style == 0) {
        char fen[MAX_FEN_STR];
        get_fen(fen);
        print_info("%s\n",fen);
    }

    /*selcet net*/
    select_net();
    
    /*generate and score moves*/
    chess_clock.set_stime(hply,true);
    generate_and_score_moves(-MATE_SCORE,MATE_SCORE);

    /*no move*/
    if(pstack->count == 0) {
        return 0;
    }
    /*only one move*/
    if(pstack->count == 1) {
        pstack->pv_length = 1;
        pstack->pv[0] = pstack->move_st[0];
        print_pv(root_score);
        return pstack->move_st[0];
    }

#ifdef BOOK_PROBE
    /*find book move*/
    if(book_loaded 
        && hply <= last_book_move + 6 
        ) {
            MOVE move = get_book_move();
            if(move) {
                last_book_move = hply;
                pstack->pv_length = 1;
                pstack->pv[0] = move;
                print_pv(0);
                return move;
            }
    }
#endif

    stack[0].pv[0] = pstack->move_st[0];
    
    /*
    Iterative deepening
    */

    MOVE bmove = 0;

#ifdef PARALLEL
    /*wakeup threads*/
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        processors[i]->state = WAIT;
    t_sleep(30);
#endif

    bmove = iterative_deepening();

#ifdef PARALLEL
    /*park threads*/
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        processors[i]->state = PARK;
#endif

    /*
    park hosts
    */
#ifdef CLUSTER
    if(PROCESSOR::host_id == 0 && use_abdada_cluster) {
        for(int i = 1;i < PROCESSOR::n_hosts;i++)
            PROCESSOR::ISend(i,PROCESSOR::QUIT);
    }
#endif

    /*was this first search?*/
    if(first_search) {
        first_search = false;
    }

    return bmove;
}
void SEARCHER::print_status() {
    if(!SEARCHER::abort_search) {
        char mv_str[16];
        int time_used = get_time() - SEARCHER::start_time;
        if(!montecarlo) {
            mov_str(stack[0].current_move,mv_str);
            print("stat01: %d " FMT64 " %d %d %d %s\n",time_used / 10,nodes,
                search_depth,
                stack[0].count - stack[0].current_index,
                stack[0].count,
                mv_str);
        } else {
            mov_str(stack[0].pv[0],mv_str);
            print("stat01: %d " FMT64 " %d %d %d %s\n",time_used / 10,playouts,
                stack[0].pv_length,
                0,
                1,
                mv_str);
        }
    }
}
/*
* Search parameters
*/
bool check_search_params(char** commands,char* command,int& command_num) {
    if(!strcmp(command, "use_singular")) {
        use_singular = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "use_probcut")) {
        use_probcut = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "singular_margin")) {
        singular_margin = atoi(commands[command_num++]);
    } else if(!strcmp(command, "probcut_margin")) {
        probcut_margin = atoi(commands[command_num++]);
    } else if(!strcmp(command, "alphabeta_man_c")) {
        alphabeta_man_c = atoi(commands[command_num++]);
    } else if(!strcmp(command, "multipv")) {
        multipv = is_checked(commands[command_num++]);
#ifdef TUNE
    } else if(!strncmp(command, "futility_margin",15)) {
        futility_margin[atoi(&command[15])] = atoi(commands[command_num++]);
    } else if(!strncmp(command, "razor_margin",12)) {
        razor_margin[atoi(&command[12])] = atoi(commands[command_num++]);
    } else if(!strncmp(command, "failhigh_margin",15)) {
        failhigh_margin[atoi(&command[15])] = atoi(commands[command_num++]);
    } else if(!strncmp(command, "lmp_count",9)) {
        lmp_count[atoi(&command[9])] = atoi(commands[command_num++]);
    } else if(!strncmp(command, "lmr_count",9)) {
        lmr_count[atoi(&command[9])] = atoi(commands[command_num++]);
    } else if(!strncmp(command, "lmr_root_count",14)) {
        lmr_root_count[atoi(&command[14])] = atoi(commands[command_num++]);
    } else if(!strcmp(command, "lmr_all_count")) {
        lmr_all_count = atoi(commands[command_num++]);
    } else if(!strcmp(command, "lmr_cut_count")) {
        lmr_cut_count = atoi(commands[command_num++]);
    } else if(!strcmp(command, "aspiration_window")) {
        aspiration_window = atoi(commands[command_num++]);
#endif
    } else if(!strcmp(command, "contempt")) {
        contempt = atoi(commands[command_num++]);
    } else if(!strcmp(command, "smp_type")) {
        command = commands[command_num++];
#ifdef PARALLEL
        if(!strcmp(command,"YBW")) use_abdada_smp = 0;
        else if(!strcmp(command,"ABDADA")) use_abdada_smp = 1;
        else use_abdada_smp = 2;
#endif
    } else if(!strcmp(command, "cluster_type")) {
        command = commands[command_num++];
#ifdef CLUSTER
        if(!strcmp(command,"YBW")) use_abdada_cluster = 0;
        else if(!strcmp(command,"ABDADA")) use_abdada_cluster = 1;
        else use_abdada_cluster = 2;
#endif
    } else if(!strcmp(command, "smp_depth")) {
        SMP_CODE(PROCESSOR::SMP_SPLIT_DEPTH = atoi(commands[command_num]));
        command_num++;
    } else if(!strcmp(command, "cluster_depth")) {
        CLUSTER_CODE(PROCESSOR::CLUSTER_SPLIT_DEPTH = atoi(commands[command_num]));
        command_num++;
    } else if(!strcmp(command, "message_poll_nodes")) {
        CLUSTER_CODE(PROCESSOR::MESSAGE_POLL_NODES = atoi(commands[command_num]));
        command_num++;
    } else {
        return false;
    }
    return true;
}
void print_search_params() {
    static const char* parallelt[] = {"YBW", "ABDADA", "SHT"};
    SMP_CODE(print_combo("smp_type",parallelt,use_abdada_smp,3));
    SMP_CODE(print_spin("smp_depth",PROCESSOR::SMP_SPLIT_DEPTH,1,10));
    CLUSTER_CODE(print_combo("cluster_type",parallelt,use_abdada_cluster,3));
    CLUSTER_CODE(print_spin("cluster_depth",PROCESSOR::CLUSTER_SPLIT_DEPTH,1,16));
    CLUSTER_CODE(print_spin("message_poll_nodes",PROCESSOR::MESSAGE_POLL_NODES,10,20000));
    print_check("use_singular",use_singular);
    print_check("use_probcut",use_probcut);
    print_spin("singular_margin",singular_margin,0,1000);
    print_spin("probcut_margin",probcut_margin,0,1000);
    print_spin("contempt",contempt,0,100);
    print_spin("alphabeta_man_c",alphabeta_man_c,0,32);
    print_check("multipv",multipv);
}
