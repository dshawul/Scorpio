#include "scorpio.h"

/* search options */
int contempt = 10;
static int alphabeta_man_c = 12;
static int multipv = 0;
static int multipv_margin = 100;
/* search features */
const int use_nullmove = 1;
const int use_selective = 1;
const int use_aspiration = 1;
const int use_iid = 0;
const int use_pvs = 1;
const int use_probcut = 0;
const int use_singular = 1;

/* tunable search parameters */
#ifdef TUNE
#   define PARAM int
#else
#   define PARAM const int
#endif

static PARAM aspiration_window = 10;
static PARAM failhigh_margin = 120;
static PARAM razor_margin = 120;
static PARAM futility_margin = 130;
static PARAM probcut_margin = 195;
static PARAM lmp_count[] = {0, 10, 10, 15, 21, 24, 44, 49};
static PARAM lmr_count[2][8] = {{2, 4, 6, 8, 16, 32, 48, 64},
                                {1, 2, 3, 4,  8, 16, 24, 32}};
static PARAM lmr_ntype_count[] = {17, 5, 5};

/* shared variables */
static int prev_pv_length = 0;
int qsearch_level = 0;

static int multipv_count = 0;
static int multipv_bad = 0;

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
    if(!montecarlo && (use_abdada_smp == 1) && ply > 1
        && PROCESSOR::n_processors > 1
        && (pstack - 1)->depth > PROCESSOR::SMP_SPLIT_DEPTH
        && !(pstack - 1)->second_pass
        && (pstack - 1)->legal_moves > 1
        ) {
        exclusiveP = true;
    }

    /*probe*/
    pstack->hash_move = 0;
    pstack->hash_eval = -MATE_SCORE;
    pstack->hash_flags = PROBE_HASH(player,hash_key,pstack->depth,ply,pstack->hash_eval,
        pstack->hash_score,pstack->hash_move,pstack->alpha,pstack->beta,pstack->mate_threat,
        pstack->singular,pstack->hash_depth,exclusiveP);

    if(exclusiveP) {
        if(pstack->hash_flags == CRAP) {
            /*move is taken!*/
            pstack->best_score = SKIP_SCORE;
            return true;
        } else if(pstack->hash_flags == HASH_HIT) {
            /*we had a hit and replaced the flag with load of CRAP (depth=255)*/
        } else if(pstack->hash_flags == UNKNOWN) {
            /*store new crap*/
            RECORD_HASH(player,hash_key,255,0,CRAP,0,0,0,0,0);
        }
    }

    /*legality checking*/
    if(pstack->hash_move
        && !is_legal_fast(pstack->hash_move)
        ) {
        pstack->hash_move = 0;
    }

    /*update history*/
    if(pstack->hash_move && !is_cap_prom(pstack->hash_move)) {
        pstack->current_index = 1;
        if(pstack->hash_flags == LOWER)
            update_history(pstack->hash_move);
        else if(pstack->hash_flags == UPPER)
            update_history(pstack->hash_move,true);
    }

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
            pstack->qcheck_depth = 2; 
        else 
            pstack->qcheck_depth = 1;
        qsearch_nn();
        return true;
    }

    /*initialize node*/
    pstack->gen_status = GEN_START;
    pstack->flag = UPPER;
    pstack->pv_length = ply;
    pstack->legal_moves = 0;
    pstack->best_score = -MATE_SCORE;
    pstack->best_move = 0;
    pstack->mate_threat = 0;
    pstack->singular = 0;
    pstack->all_done = true;
    pstack->second_pass = false;
    pstack->static_eval = -MATE_SCORE;

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

    /*mate distance pruning*/
    if(pstack->alpha > MATE_SCORE - WIN_PLY * (ply)) {
        pstack->best_score = pstack->alpha;
        return true; 
    }

    /*3-fold and 50 mr*/
    if(draw()) {
        pstack->best_score = 
            ((scorpio == player) ? -contempt : contempt);
        return true;
    }

    /*ply limit*/
    if(ply >= MAX_PLY - 1) {
        pstack->best_score = eval();
        return true;
    }

    /*probe hash table*/
    if(hash_cutoff())
        return true;

    /*bitbase cutoff*/
    if(bitbase_cutoff())
        return true;

    /*iid replacement?*/
    if(!use_iid
        && !pstack->hash_move
        && pstack->depth >= 6
        && pstack->node_type != ALL_NODE
        ) {
        pstack->depth--;
        if(pstack->node_type == PV_NODE)
            pstack->depth--;
    }

    /*static evaluation of position*/
    if(hply >= 1 && !hstack[hply - 1].checks) {
        if(pstack->hash_flags != UNKNOWN
            && pstack->hash_flags != CRAP
            && pstack->hash_eval != -MATE_SCORE
            ) {
            pstack->static_eval = pstack->hash_eval;
        } else
            pstack->static_eval = eval();

        pstack->improving = 
            (ply < 2 || pstack->static_eval >= (pstack - 2)->static_eval);
    } else
        pstack->improving = true;

    /*razoring & static pruning*/
    if(use_selective
        && pstack->depth <= 7
        && all_man_c > MAX_EGBB
        && (pstack - 1)->search_state != NULL_MOVE
        && !hstack[hply - 1].checks
        && pstack->node_type != PV_NODE
        ) {
            int score = pstack->static_eval;
            int rmargin = razor_margin * pstack->depth;
            int fhmargin = failhigh_margin * pstack->depth;

            if(score + rmargin <= pstack->alpha
                && pstack->depth <= 4
                ) {
                pstack->qcheck_depth = 1;
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
                /*pruning*/
                score = pstack->best_score;
                if(score + rmargin <= pstack->alpha)
                    return true;
                /*re-initialize node*/
                {
                    pstack->gen_status = GEN_START;
                    pstack->flag = UPPER;
                    pstack->pv_length = ply;
                    pstack->legal_moves = 0;
                    pstack->best_score = -MATE_SCORE;
                    pstack->best_move = 0;
                }
            } else if(score - fhmargin >= pstack->beta) {
                pstack->best_score = score - fhmargin;
                return true;
            }
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

    return false;
}

FORCEINLINE int SEARCHER::on_qnode_entry() {

    /*initialize node*/
    pstack->gen_status = GEN_START;
    pstack->pv_length = ply;
    pstack->best_score = -MATE_SCORE;
    pstack->best_move = 0;
    pstack->legal_moves = 0;
    pstack->flag = UPPER;
    pstack->static_eval = -MATE_SCORE;

    if(pstack->node_type == PV_NODE) {
        pstack->next_node_type = PV_NODE;
    } else if(pstack->node_type == ALL_NODE) {
        pstack->next_node_type = CUT_NODE;
    } else {
        pstack->next_node_type = ALL_NODE;
    }

    /*mate distance pruning*/
    if(pstack->alpha > MATE_SCORE - WIN_PLY * ply) {
        pstack->best_score = pstack->alpha;
        return true; 
    }

    /*3-fold and 50 mr*/
    if(draw()) {
        pstack->best_score = 
            ((scorpio == player) ? -contempt : contempt);
        return true;
    }

    /*ply limit*/
    if(ply >= MAX_PLY - 1) {
        pstack->best_score = eval();
        return true;
    }

    /*probe hash table*/
    if(hash_cutoff())
        return true;

    /*bitbase cutoff*/
    if(bitbase_cutoff())
        return true;

    /*static evaluation of position*/
    if(hply >= 1 && !hstack[hply - 1].checks) {
        if(pstack->hash_flags != UNKNOWN
            && pstack->hash_flags != CRAP
            && pstack->hash_eval != -MATE_SCORE
            ) {
            pstack->static_eval = pstack->hash_eval;
        } else
            pstack->static_eval = eval();
    }

    /*stand pat*/
    if((hply >= 1 && !hstack[hply - 1].checks) || !ply) {
        int score = pstack->static_eval;
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
            if(rank(m_from(move)) == RANK6) return 1;
        } else {
            if(rank(m_from(move)) == RANK3) return 1;
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
    int extension = 0,score,depth = (pstack - 1)->depth;
    int node_t = (pstack - 1)->node_type;
    bool improving = (pstack - 1)->improving;

    pstack->extension = 0;
    pstack->reduction = 0;

    /*root*/
    if(ply == 1) {
        if(!hstack[hply - 1].checks
            && !is_cap_prom(move)
            && !is_pawn_push(move)
            ) {
            for(int i = 0; i < 8; i++) {
                if(nmoves >= lmr_count[0][i] && pstack->depth > 1) {
                    reduce(1);
                } else break;
            }
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
    if(hstack[hply - 1].checks || is_pawn_push(move)) {
        if(node_t == PV_NODE) { extend(1); }
        else { extend(0); }
    }
    if(m_capture(move)
        && piece_c[white] + piece_c[black] == 0
        && PIECE(m_capture(move)) != pawn
        ) {
        extend(1);
    }
    if((pstack - 1)->singular && nmoves == 1) {
        extend(1);
    }
    if(extension > 1)
        extension = 1;

    pstack->depth += extension; 
    /*
    pruning
    */
    if(all_man_c > MAX_EGBB
        && !hstack[hply - 1].checks
        && noncap_reduce
        && ABS((pstack - 1)->best_score) != MATE_SCORE
        ) {
            //late move pruning
            if(depth <= 7) {
                int clmp = (improving ? lmp_count[depth] : lmp_count[depth] / 2);
                if(nmoves >= clmp)
                    return true;
            }
            //see pruning
            if(PIECE(m_piece(move)) != king
                && piece_see_v[m_piece(move)] >= 100 * depth 
                && see(move) <= -100 * depth)
                return true;
            //history pruning
            const MOVE& cMove = hstack[hply - 2].move;
            if(depth <= 3 && REF_FUP_HISTORY(cMove,move) <= -(MAX_HIST >> 4))
                return true;
            //futility pruning
            if(depth <= 7) { 
                score = (pstack - 1)->static_eval;
                if(score + futility_margin * depth < (pstack - 1)->alpha) {
                    if(score > (pstack - 1)->best_score) {
                        (pstack - 1)->best_score = score;
                        (pstack - 1)->best_move = move;
                    }
                    return true;
                }
            }
    }
    /*
    late move reduction
    */
    if(noncap_reduce && nmoves >= 2) {
        //by number of moves searched so far including current move
        for(int i = 0; i < 8; i++) {
            int lim = ((64 - depth) * lmr_count[0][i] + 
                       (depth -  0) * lmr_count[1][i]) >> 6;
            if(nmoves >= lim && pstack->depth > 1) {
                reduce(1);
            } else break;
        }
        //lets find more excuses to reduce
        //all and cut nodes
        if(nmoves >= lmr_ntype_count[node_t]
            && pstack->depth > 1
            ) { 
            reduce(1);
            if(node_t == CUT_NODE && pstack->depth > 1)
                reduce(1);
        }
        //has tt move
        if((pstack-1)->hash_move == (pstack-1)->best_move) {
            //reduce late moves
            if(nmoves >= 8 && pstack->depth > 1) { reduce(1); }
            //capture tt move
            if(is_cap_prom((pstack-1)->hash_move)) {
                if(pstack->depth > 1) { reduce(1); }
                if(nmoves >= 8 && pstack->depth > 1) { reduce(1); }
            }
        }
        //see reduction
        if(depth > 7 && pstack->depth > 1 && see(move) < 0) {
            reduce(1);
            if(nmoves >= 8 && pstack->depth > 1) { reduce(1); }
        }
        //eval not improving
        if(!improving && pstack->depth > 1)
            reduce(1);
        //reduce killers less
        if(pstack->reduction && (pstack - 1)->gen_status - 1 == GEN_KILLERS)
            reduce(-1);
        //reduce extended moves less
        if(pstack->extension) {
            reduce(-pstack->reduction / 2);
        }
    }
    /*losing captures*/
    if(loscap_reduce && nmoves >= 2) {
        if(pstack->extension) {
            reduce(1);
        } else if(pstack->depth <= 2)
            return true;
        else if(pstack->depth > 4) {
            reduce(pstack->depth / 2);
        } else {
            reduce(2);
        }
    }
#if 0
    /* slow neural network pruning*/
    if(use_nn && !skip_nn && nmoves >= 3) {
        reduce(4);
        if(pstack->depth <= 0)
            return true;
    }
#endif
    /*
    end
    */
    return false;
}

/*
Back up to previous ply
*/
#define GOBACK(save) {                                                                                          \
    if(save && !sb->abort_search && !sb->stop_searcher &&                                                       \
        ((sb->pstack->search_state & ~MOVE_MASK) != SINGULAR_SEARCH)) {                                         \
        sb->RECORD_HASH(                                                                                        \
            sb->player,sb->hash_key,sb->pstack->depth,sb->ply,sb->pstack->flag,sb->pstack->static_eval,         \
            sb->pstack->best_score,sb->pstack->best_move,sb->pstack->mate_threat,sb->pstack->singular);         \
    }                                                                                                           \
    if(sb->pstack->search_state & ~MOVE_MASK) goto SPECIAL;                                                     \
    goto POP;                                                                                                   \
};
/*
Iterative search.
*/
void search(PROCESSOR* const proc, bool single)
{
    MOVE move;
    int score = 0;
    PSEARCHER sb = proc->searcher;
    int active_workers;
    CLUSTER_CODE(int active_hosts);

    /*
    * First processor goes on searching, while the
    * rest go to sleep mode.
    */
    if(sb && proc->state == GO) {
        sb->stop_ply = sb->ply;
        goto NEW_NODE;
    } else {
        goto IDLE_START;
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
                    && sb->pstack->depth >= 6
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
                    sb->pstack->depth -= 4;
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
                    && sb->pstack->depth >= 6
                    ) {
                    sb->pstack->o_alpha = sb->pstack->alpha;
                    sb->pstack->o_beta = sb->pstack->beta;
                    sb->pstack->o_depth = sb->pstack->depth;
                    if(sb->pstack->node_type == PV_NODE) 
                        sb->pstack->depth -= 2;
                    else 
                        sb->pstack->depth -= 4;
                    sb->pstack->search_state |= NORMAL_MOVE; 
                } else {
                    sb->pstack->search_state = SINGULAR_SEARCH;
                    goto START;
                }
                break;
            case SINGULAR_SEARCH:
                if(use_singular
                    && sb->pstack->hash_move
                    && sb->pstack->depth >= 8
                    && sb->pstack->hash_flags == HASH_GOOD
                    && !sb->pstack->singular
                    ) {
                        sb->pstack->o_alpha = sb->pstack->alpha;
                        sb->pstack->o_beta = sb->pstack->beta;
                        sb->pstack->o_depth = sb->pstack->depth;
                        sb->pstack->alpha = sb->pstack->hash_score - sb->pstack->depth;
                        sb->pstack->beta = sb->pstack->alpha + 1;
                        sb->pstack->depth /= 2;
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
                    && sb->pstack->depth >= 4
                    && sb->pstack->node_type != PV_NODE
                    && sb->piece_c[sb->player]
                    && (score = (sb->pstack->static_eval - sb->pstack->beta)) >= 0
                    ) {
                        sb->PUSH_NULL();
                        sb->nodes++;
                        sb->pstack->extension = 0;
                        sb->pstack->reduction = 0;
                        sb->pstack->alpha = -(sb->pstack - 1)->beta;
                        sb->pstack->beta = -(sb->pstack - 1)->beta + 1;
                        sb->pstack->node_type = (sb->pstack - 1)->next_node_type;
                        /* Smooth scaling from Dann Corbit based on score and depth*/
                        sb->pstack->depth = (sb->pstack - 1)->depth - 3 - 
                                            (sb->pstack - 1)->depth / 4 -
                                            (MIN(3 , score / 128));
                        /*search normal move after null*/
                        sb->pstack->search_state = NORMAL_MOVE;
                        goto NEW_NODE;
                }
                sb->pstack->search_state = PROBCUT_SEARCH;
                goto START;
            case NORMAL_MOVE:
                while(true) {
                    /*
                    * Get Smp move
                    */
                    if(!use_abdada_smp && !single
                        && sb->master && sb->stop_ply == sb->ply) {
                        if(!sb->get_smp_move())
                            GOBACK(false); //I
                    /*
                    * Get a move in the regular manner
                    */
                    } else {
                        if(!sb->get_move()) {
                            if(!sb->pstack->legal_moves) {
                                if(sb->hstack[sb->hply - 1].checks)
                                    sb->pstack->best_score = -MATE_SCORE + WIN_PLY * (sb->ply + 1);
                                else 
                                    sb->pstack->best_score = 0;
                                sb->pstack->flag = EXACT;
                            } else if((use_abdada_smp == 1)
                                && !sb->pstack->all_done
                                && !sb->pstack->second_pass
                                ) {
                                    sb->pstack->second_pass = true;
                                    sb->pstack->all_done = true;
                                    sb->pstack->gen_status = GEN_RESET;
                                    continue;
                            }
                            GOBACK(true);
                        } else {
                            if((use_abdada_smp == 1)
                                && !sb->pstack->all_done
                                && sb->pstack->second_pass
                                && sb->pstack->score_st[sb->pstack->current_index - 1] != -SKIP_SCORE
                                ) {
                                    sb->pstack->legal_moves++;
                                    continue;
			                }
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
                    /*prefetch tt entry*/
                    if(!is_special(sb->pstack->current_move))
                        sb->prefetch_tt(sb->get_key_after(sb->pstack->current_move));
                    /*
                    * play the move
                    */
                    sb->PUSH_MOVE(sb->pstack->current_move);
                    sb->nodes++;

                    /*set next ply's depth and be selective*/           
                    sb->pstack->depth = (sb->pstack - 1)->depth - 1;
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
            /*remeber skipped moves*/
            if((use_abdada_smp == 1)
                && score == -SKIP_SCORE
                ) {
                sb->POP_MOVE();
                sb->pstack->all_done = false;
                sb->pstack->score_st[sb->pstack->current_index - 1] = score;
                continue;
            }
            /*research with full depth*/
            if(sb->pstack->reduction > 0
                && score > (sb->pstack - 1)->alpha
                ) {
                sb->pstack->depth++;
                sb->pstack->reduction--;
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
            /*check if second/multipv move is weak by a big margin*/
            if(sb->ply == 1
                && (sb->pstack - 1)->legal_moves > 1
                && (sb->pstack - 1)->legal_moves <= 1 + multipv_count
                && score <= (sb->pstack - 1)->alpha
                && sb->pstack->beta < -(sb->pstack - 1)->alpha + multipv_margin
                ) {
                sb->pstack->alpha = -(sb->pstack - 1)->alpha + multipv_margin - 1;
                sb->pstack->beta = -(sb->pstack - 1)->alpha + multipv_margin;
                sb->pstack->node_type = CUT_NODE;
                sb->pstack->search_state = NULL_MOVE;
#if 0
                char mvs[16];
                mov_strx((sb->pstack - 1)->current_move,mvs);
                printf("Researching multipv move %s with [%d, %d]\n",
                    mvs,-sb->pstack->beta,-sb->pstack->alpha);
#endif
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
                SEARCHER::root_nodes[sb->pstack->current_index - 1] 
                      += (sb->nodes - sb->pstack->start_nodes);
                if(sb->pstack->current_index == 1 || score > sb->pstack->alpha) {
                    SEARCHER::root_scores[sb->pstack->current_index - 1]
                          = score;
                }
                l_unlock(lock_smp);

                /*check if NN best score (second best) is a blunder*/
                if(sb->pstack->legal_moves > 1
                    && sb->pstack->legal_moves <= 1 + multipv_count
                    ) {
                    if(score <= sb->pstack->alpha - multipv_margin)
                        multipv_bad = 1;
                    else
                        multipv_bad = 0;
#if 0
                    char mv0[16],mv1[16];
                    mov_strx(move,mv1);
                    mov_strx(sb->pstack->move_st[0],mv0);
                    printf("%s alternative %s to main pv %s: %d %d\n",
                        multipv_bad ? "Bad" : "Ok",
                        mv1,mv0,score,sb->pstack->alpha);
#endif
                }
#if 0
                print("%d. %d [%d %d] " FMT64 "\n",
                    sb->pstack->current_index,score,
                    -(sb->pstack+1)->beta,-(sb->pstack+1)->alpha,
                    SEARCHER::root_nodes[sb->pstack->current_index - 1]);
#endif

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
                    else if(!use_abdada_smp || !sb->processor_id)
                        sb->print_pv(sb->pstack->best_score);

                    if(!sb->chess_clock.pondering)
                        SEARCHER::root_score = score;

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
                    /* stop workers*/
                    if(!use_abdada_smp && !single && 
                        sb->master && sb->stop_ply == sb->ply)
                        sb->master->handle_fail_high();
                    GOBACK(true);
                }
                sb->pstack->alpha = score;
                sb->pstack->flag = EXACT;
                sb->UPDATE_PV(move);
            }
            /* Check for split here since at least one move is searched now.
             * Reset the sb pointer to the child block pointed to by this processor.*/
            if(!use_abdada_smp && !single
                && (sb->ply || sb->pstack->legal_moves >= 4) 
                && sb->check_split())
                sb = proc->searcher;
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
            else if(sb->pstack->beta >= sb->pstack->o_beta)
                goto POP;
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
    if(save && !abort_search && !stop_searcher) {                   \
        RECORD_HASH(player,hash_key,0,ply,pstack->flag,             \
                    pstack->static_eval,pstack->best_score,         \
                    pstack->best_move,0,0);                         \
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

            /*prefetch tt entry*/
            if(!is_special(pstack->current_move))
                prefetch_tt(get_key_after(pstack->current_move));

            pstack->legal_moves++;

            PUSH_MOVE(pstack->current_move);
            nodes++;
            qnodes++;

            /*next ply's window and depth*/ 
            pstack->alpha = -(pstack - 1)->beta;
            pstack->beta = -(pstack - 1)->alpha;
            pstack->depth = (pstack - 1)->depth - 1;
            pstack->node_type = (pstack - 1)->next_node_type;
            pstack->qcheck_depth = (pstack - 1)->qcheck_depth - 1;
            /*end*/

NEW_NODE_Q:     
            /*
            NEW node entry point
            */
            if( (!finish_search &&
                (stop_searcher || abort_search) ) ||
                on_qnode_entry() ) {
                seldepth = MAX(seldepth, unsigned(ply));
                goto POP_Q;
            }
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
}

void SEARCHER::search() {

    /*mcts*/
    if(montecarlo) {
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
    /*alphabeta*/
    } else {
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
    }
}
/*
Get search score
*/
int SEARCHER::search_ab() {
    if(pstack->depth <= 0) qsearch_calls++;
    else search_calls++;
    ::search(processors[processor_id],true);
    return pstack->best_score;
}
/*
Evaluate moves with search
*/
void SEARCHER::evaluate_moves(int depth) {
    int alpha = -MATE_SCORE;
    int beta = MATE_SCORE;
    int score;

    finish_search = true;

    /*sort tt move*/
    if(depth > 0) {
        hash_cutoff();
        for(int i = 0; i < pstack->count;i++) {
            if(pstack->move_st[i] == pstack->hash_move) {
                pstack->move_st[i] = pstack->move_st[0];
                pstack->move_st[0] = pstack->hash_move;
                break;
            }
        }
    }

    for(int i = 0;i < pstack->count; i++) {
        pstack->current_move = pstack->move_st[i];
        PUSH_MOVE(pstack->current_move);

        nodes++;
        if(depth <= 0) qnodes++;

        /*extensions and reductions*/
        int newd = depth;
        pstack->extension = 0;
        pstack->reduction = 0;
        if(depth > 0) {
            for(int j = 0;j < 8;j++) {
                if(i >= lmr_count[0][j] && newd > 1) {
                    newd--;
                    pstack->reduction++;
                }
            }
            if(i > lmr_count[0][0]) {
                if(hstack[hply - 1].checks) {
                    newd++;
                    pstack->extension++;
                }
            }
        }

        /*search move*/
        if(i == 0 || depth <= 0) {
TOP:
            pstack->alpha = -beta;
            pstack->beta = -alpha;
            pstack->node_type = PV_NODE;
        } else {
            pstack->alpha = -alpha - 1;
            pstack->beta = -alpha;
            pstack->node_type = CUT_NODE;
        }
        pstack->depth = newd;
        pstack->search_state = NULL_MOVE;

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

        /*research*/
        if(depth > 0 && score > alpha) {
            if(pstack->node_type == CUT_NODE || newd < depth) {
                if(newd < depth) newd++;
                goto TOP;
            }
        }

        /*end search move*/
        POP_MOVE();

        if(!ply && abort_search)
            break;

        /*better score*/
        if(depth > 0 && score > alpha)
            alpha = score;

        pstack->score_st[i] = score;
    }

    finish_search = false;

    pstack->sort_all();
}
/*
Prior AB search
*/
void SEARCHER::search_ab_prior() {
    /*
      Do regular AB search and use subtree size of root moves
      to set prior for the following montecarlo search
    */
    montecarlo = 0;
    use_nn = 0;

    chess_clock.p_time *= frac_abprior;
    chess_clock.p_inc *= frac_abprior;

    bool dummy;
    MOVE bestm = iterative_deepening(dummy);

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
    for(int i = 1; i < pstack->count; i++) {
        uint64_t v = root_nodes[i];
        if(v > maxn) maxn = v;
    }

    if(root_nodes[0] < 1.5 * maxn)
        root_nodes[0] = 1.5 * maxn;

    /*assign prior*/
    Node* current = root_node->child;
    while(current) {
        for(int i = 0;i < pstack->count; i++) {
            MOVE& move = pstack->move_st[i];
            if(move == current->move) {
                double v = sqrt(double(root_nodes[i]) / maxn);
                current->prior = -(v - 0.5) * 100;
                if(rollout_type == MCTS)
                    current->prior = logistic(current->prior);
                current->rank = i + 1;
                if(move == bestm) current->policy += 0.1;
#if 0
                char mvs[16];
                mov_str(move,mvs);
                print("%2d. %6s %9.6f %9.6f " FMT64W " " FMT64W "\n", 
                    current->rank, mvs, current->prior,
                    v, root_nodes[i], maxn);
#endif
                break;
            }
        }
        current = current->next;
    }
}
/*
Iterative deepening
*/
MOVE SEARCHER::iterative_deepening(bool& montecarlo_skipped) {
    int score;
    int easy = false,easy_score = 0;
    MOVE easy_move = 0;
    bool opponent_move_expected = (hply >= 1 && expected_move == hstack[hply - 1].move);

    search_depth = 1;
    poll_nodes = 5000;
    PROCESSOR::age = (hply & AGE_MASK);
    show_full_pv = false;
    freeze_tree = false;
    multipv_count = 0;
    multipv_bad = 0;

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

    /*prepare montecarlo search*/
    if(montecarlo) {

        manage_tree();

        /*Take out so far spent time in the last move*/
        if(chess_clock.maximum_time <= 2 * chess_clock.search_time)
            chess_clock.p_time -= (get_time() - start_time);

        /*Prior Alpha-beta search */
        has_ab = (frac_abprior > 0) &&
            !is_selfplay && (chess_clock.max_visits == MAX_NUMBER) &&
            ((all_man_c <= alphabeta_man_c && root_score >= -80) || root_score >= 400);

        if(has_ab) {
            /*park mcts threads*/
            for(int i = PROCESSOR::n_cores;i < PROCESSOR::n_processors;i++)
                PROCESSOR::park(i);

            /*prior ab search*/
            search_ab_prior();

            /* No further search in these cases */
            if(root_score >= 700 ||
                search_depth >= MAX_PLY - 4 ||
                frac_abprior >= 0.95) {
                montecarlo_skipped = true;
                return stack[0].pv[0];
            }

            /*reset flags for mcts*/
            stop_searcher = 0;
            abort_search = 0;
            search_depth = MIN(search_depth + mcts_strategy_depth, MAX_PLY - 2);

            /* wake mcts threads*/
            for(int i = PROCESSOR::n_cores;i < PROCESSOR::n_processors;i++)
                PROCESSOR::wait(i);
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

        /*egbb ply limit*/
        if(!montecarlo)
            SEARCHER::egbb_ply_limit = 
                SEARCHER::egbb_ply_limit_percent * search_depth / 100;
        else
            SEARCHER::egbb_ply_limit = 8;

        /*Set bounds and search.*/
        pstack->depth = search_depth;
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

        /*determine time factor*/
        time_factor = 1.0;
        compute_time_factor(root_score);

        if(!montecarlo && opponent_move_expected) {
            float factor = (1 - MIN(0.9, (time_factor - 1.0) / 3));
            if(chess_clock.p_time >= 0.8 * chess_clock.o_time)
                factor = MIN(1.0, factor + 0.2);
            time_factor *= factor;
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
                            int(-current->score),int(current->visits),
                            -current->beta, -current->alpha);
                        break;
                    }
                    current = current->next;
                }
            }
        }
#endif

        /*sort moves*/
        if(!montecarlo) {
            /* Is there enough time to search the first move?
             * First move taking lot more time than second. */
            if(!root_failed_low && chess_clock.is_timed()) {
                int time_used = get_time() - start_time;
                double ratio = double(root_nodes[0]) / root_nodes[1];
                if((time_used >= 0.75 * time_factor * chess_clock.search_time) || 
                   (time_used >= 0.5 * time_factor * chess_clock.search_time && ratio >= 2.0)  ) {
                    abort_search = 1;
                    break;
                }
            }

#ifdef CLUSTER
            /*install pvs of other nodes*/
            if(use_abdada_cluster && PROCESSOR::n_hosts > 1) {
                l_lock(lock_smp);
                for(int j = 0;j < PROCESSOR::n_hosts;j++) {
                    if(j != PROCESSOR::host_id) {
                        int depth = 0;
                        for(int i = 0;i < PROCESSOR::node_pvs[j].pv_length;i++) {
                            MOVE mv = PROCESSOR::node_pvs[j].pv[i];
                            if(is_legal_fast(mv)) {
                                RECORD_HASH(player,hash_key,0,0,HASH_HIT,0,0,mv,0,0);
                                PUSH_MOVE(mv);
                                depth++;
                            } else break;
                        }
                        for(int i = 0;i < depth;i++)
                            POP_MOVE();
                    }
                }
                l_unlock(lock_smp);
            }
#endif
            /*install fake pv into TT table so that it is searched
            first incase it was overwritten*/
            if(pstack->pv_length) {
                for(int i = 0;i < stack[0].pv_length;i++) {
                    RECORD_HASH(player,hash_key,0,0,HASH_HIT,0,0,stack[0].pv[i],0,0);
                    PUSH_MOVE(stack[0].pv[i]);
                }
                for(int i = 0;i < stack[0].pv_length;i++)
                    POP_MOVE();
            }

            /*bias moves found to be best by other hosts and us*/
            multipv_count = 0;
            uint64_t bests = 0;
            for(int i = 0;i < pstack->count; i++) {
                MOVE& move = pstack->move_st[i];
                if(move == stack[0].pv[0]) {
                    bests = root_nodes[i];
                    root_nodes[i] = MAX_UINT64;
                }
#ifdef CLUSTER
                else if(use_abdada_cluster && PROCESSOR::n_hosts > 1) {
                    for(int j = 0;j < PROCESSOR::n_hosts;j++) {
                        if(j != PROCESSOR::host_id) {
                            if(move == PROCESSOR::node_pvs[j].pv[0])
                                root_nodes[i] += (MAX_UINT64 >> 1);
                        }
                    }
                }
#endif
            }

            /*sort*/
            for(int i = 0;i < pstack->count; i++) {
                for(int j = i + 1;j < pstack->count;j++) {
                    if(root_nodes[i] < root_nodes[j]) {
                        MOVE tempm = pstack->move_st[i];
                        uint64_t tempn = root_nodes[i];
                        int temps = root_scores[i];

                        pstack->move_st[i] = pstack->move_st[j];
                        root_nodes[i] = root_nodes[j];
                        root_scores[i] = root_scores[j];

                        pstack->move_st[j] = tempm;
                        root_nodes[j] = tempn;
                        root_scores[j] = temps;
                    }
                }
            }

            /*remove applied bias*/
            for(int i = 0;i < pstack->count; i++) {
                MOVE& move = pstack->move_st[i];
                if(root_nodes[i] == MAX_UINT64)
                    root_nodes[i] = bests;
#ifdef CLUSTER
                else if(use_abdada_cluster && PROCESSOR::n_hosts > 1) {
                    if(root_nodes[i] >= (MAX_UINT64 >> 1)) {
                        root_nodes[i] -= (MAX_UINT64 >> 1);
                        multipv_count++;
                    }
                }
#endif
            }
            if(!multipv_count) multipv_bad = 0;
        } else {
            /* Is there enough time to search the first move?*/
            if(rollout_type == ALPHABETA) {
                if(!root_failed_low && chess_clock.is_timed()) {
                    int time_used = get_time() - start_time;
                    if(time_used >= 0.75 * time_factor * chess_clock.search_time) {
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
                    pstack->depth = search_depth;
                    root_failed_low = 0;
                    freeze_tree = false;
                }
                /*alphabeta bounds*/
                root_node->alpha = alpha;
                root_node->beta = beta;
                Node::parallel_job(root_node,rank_reset_thread_proc,true);
            }
        }

        /*aspiration window search*/
        if(!use_aspiration ||
            search_depth <= 3
            ) {
            alpha = -MATE_SCORE;
            beta = MATE_SCORE;
            prev_pv_length = stack[0].pv_length;
        } else if(score <= alpha) {
            WINDOW = 3 * WINDOW / 2;
            alpha = MAX(-MATE_SCORE,score - WINDOW);
            search_depth--;
        } else if (score >= beta){
            WINDOW = 3 * WINDOW / 2;
            beta = MIN(MATE_SCORE,score + WINDOW);
            search_depth--;
        } else {
            WINDOW = aspiration_window;
            alpha = score - WINDOW;
            beta = score + WINDOW;
            prev_pv_length = stack[0].pv_length;
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

    /*print final pv*/
    if(!montecarlo) {
        for (int j = ply; j > 0 ; j--) {
            MOVE move = hstack[hply - 1].move;
            if(move) POP_MOVE();
            else POP_NULL();
        }
        if(!pstack->pv_length)
            pstack->pv_length = 1;
        print_pv(root_score);
    }

    return stack[0].pv[0];
}
/*
* Find best move using alpha-beta or mcts
*/
MOVE SEARCHER::find_best() {

    start_time = start_time_o = get_time();

    /*
    initialize search
    */
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
    seldepth = 0;
    prev_pv_length = 0;
    old_root_score = root_score;
    PROCESSOR::set_num_searchers();
    clear_history();

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
    for(int i = 0; i < pstack->count; i++) {
        root_nodes[i] = 0;
        root_scores[i] = pstack->score_st[i];
    }

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

    stack[0].pv[0] = pstack->move_st[0];

    /*
    send initial position to helper hosts
    */
#ifdef CLUSTER
    if(PROCESSOR::host_id == 0 && PROCESSOR::n_hosts > 1) {
        INIT_MESSAGE init;
        get_init_pos(&init);
        for(int i = 0;i < PROCESSOR::n_hosts;i++) {
            PROCESSOR::node_pvs[i].pv_length = 0;
            PROCESSOR::node_pvs[i].pv[0] = 0;
            if(i != PROCESSOR::host_id) {
                PROCESSOR::send_init(i,init);
                /*start iterative deepening searcher for ABDADA/SHT*/
                if(use_abdada_cluster || montecarlo)
                    PROCESSOR::ISend(i,PROCESSOR::GOROOT);
            }
        }
    }
    if(use_abdada_cluster || montecarlo)
        PROCESSOR::Barrier();
#endif

    /*
    Iterative deepening
    */

    MOVE bmove = 0;

    /*wakeup threads*/
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        PROCESSOR::wait(i);
#if defined(CLUSTER)
    PROCESSOR::set_mt_state(WAIT);
#endif

    /*iteratived deepening*/
    bool montecarlo_skipped = false;
    bmove = iterative_deepening(montecarlo_skipped);
    if(!SEARCHER::chess_clock.pondering)
        SEARCHER::expected_move = stack[0].pv[1];

    /*park threads*/
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        PROCESSOR::park(i);
#if defined(CLUSTER)
    PROCESSOR::set_mt_state(PARK);
#endif

#ifdef CLUSTER
    /*quit hosts as soon as host 0 finishes*/
    if(use_abdada_cluster || montecarlo) {
        if(PROCESSOR::host_id == 0) {
            PROCESSOR::quit_hosts();
            PROCESSOR::wait_hosts();
        } else
            PROCESSOR::ISend(0,PROCESSOR::PONG);
    }

    /*print pv with max score to avoid early adjuncation*/
    int max_root_score;
    if(use_abdada_cluster && PROCESSOR::n_hosts > 1) {
        PROCESSOR::Max(&root_score,&max_root_score,1);
        if(PROCESSOR::host_id == 0)
            print_pv(max_root_score);
    }
#endif

    /*
    Print search result info
    */

    /*search info*/
    if(montecarlo && !montecarlo_skipped) {

        /*search has ended. display some info*/
        int time_used = MAX(1,get_time() - start_time);
        int time_used_o = MAX(1,get_time() - start_time_o);
        unsigned int pps = int(playouts / (time_used / 1000.0f));
        unsigned int vps = int(root_node->visits / (time_used / 1000.0f));
        if(average_pps > 0 && pps >= 5 * average_pps);
        else average_pps = pps;

        if(pv_print_style == 1) {
            print(" %21d %8.2f %10d %10d\n",
                unsigned(root_node->visits),float(time_used) / 1000,pps,
                int(int64_t(nnecalls) / (time_used / 1000.0f)));
        } else if(pv_print_style == 0) {
            /*print tree*/
            if(multipv)
                Node::print_tree(root_node,has_ab,MAX_PLY);

            /* print search stats*/
            print_info("Stat: nodes " FMT64 " <%d%% qnodes> tbhits %d qcalls %d scalls %d time %dms nps %d eps %d\n",nodes,
                int(int64_t(qnodes) / (int64_t(nodes) / 100.0f)),
                egbb_probes,qsearch_calls,search_calls,
                time_used_o,int(int64_t(nodes) / (time_used_o / 1000.0f)),
                int(int64_t(ecalls) / (time_used_o / 1000.0f)));
            print_info("Tree: nodes %d depth %d seldepth %d visits %d Rate: vps %d pps %d nneps %d\n",
                  unsigned(Node::total_nodes), (Node::sum_tree_depth + 1) / (root_node->visits + 1),
                  Node::max_tree_depth,unsigned(root_node->visits),vps,pps,
                   int(int64_t(nnecalls) / (time_used / 1000.0f)));
        }

    } else {
        /*print subtree sizes*/
        if(multipv) {
            print_info("ID Move    Score  Nodes\n");
            print_info("-----------------------\n");
            for(int i = 0; i < pstack->count; i++) {
                char mvs[16];
                mov_str(pstack->move_st[i],mvs);
                print_info("%2d %s %6d    " FMT64 "\n",
                    i + 1, mvs, root_scores[i], root_nodes[i]);
            }
        }

        /*print search stats*/
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

    /*
    Voting from different processes
    */

#ifdef CLUSTER
    /* merge search results of all cluster nodes*/
    if(use_abdada_cluster && PROCESSOR::n_hosts > 1) {

        MOVE move_st[MAX_MOVES];
        float score_st[MAX_MOVES];
        float sum_score_st[MAX_MOVES];
        int n_root_moves = stack[0].count;

        /*scale factor based on score*/
        float factor = PROCESSOR::vote_weight / 100.0f;
        if(root_score >= 500)
            factor *= 5;
        else if(root_score >= 400)
            factor *= 3;
        else if(root_score >= 300)
            factor *= 2;
        else if(root_score >= 150)
            factor *= 1.5;
        else if(root_score >= 100)
            factor *= 1.2;

        if(montecarlo && !montecarlo_skipped) {
            /*AB has better losing score*/
            if(root_score <= -200 && 
                max_root_score > root_score + 100) {
                factor *= 0.5;
            }
            /*compute vote based on subtree size*/
            int idx = 0;
            Node* current = root_node->child;
            while(current) {
                char mv_str[16];
                mov_str(current->move,mv_str);
                move_st[idx] = current->move;
                score_st[idx] = factor * sqrt(double(current->visits) / root_node->visits);
                idx++;
                current = current->next;
            }
        } else {
            /*weigh winning AB scores more*/
            if(root_score >= 800)
                factor *= 10;
            else if(root_score >= 600 ||
                all_man_c <= 6)
                factor *= 6;
            else if(root_score >= 500 ||
                all_man_c <= 8)
                factor *= 4;
            else if(root_score >= 400 ||
                all_man_c <= 10)
                factor *= 3;
            else if(root_score >= 300 ||
                all_man_c <= 12)
                factor *= 2.5;
            else if(root_score >= 250 ||
                all_man_c <= 14)
                factor *= 2.0;
            else if(root_score >= 200 ||
                all_man_c <= 16)
                factor *= 1.5;

            /*NN move is bad with multipv margin*/
            if(multipv_bad)
                factor *= 4;

            /*compute vote based on subtree size alone*/
            uint64_t maxn = 0;
            for(int i = 1; i < n_root_moves; i++) {
                uint64_t v = root_nodes[i];
                if(v > maxn) maxn = v;
            }
            if(root_nodes[0] < 1.5 * maxn)
                root_nodes[0] = 1.5 * maxn;

            double total_nodes = 0;
            for(int i = 0; i < n_root_moves; i++)
                total_nodes += double(root_nodes[i]);

            for(int i = 0; i < n_root_moves; i++) {
                move_st[i] = stack[0].move_st[i];
                score_st[i] = factor * sqrt(double(root_nodes[i]) / total_nodes);
            }
        }

        /*sort by move*/
        for(int i = 0;i < n_root_moves; i++) {
            for(int j = i + 1;j < n_root_moves;j++) {
                if(move_st[i] < move_st[j]) {
                    MOVE tempm = move_st[i];
                    move_st[i] = move_st[j];
                    move_st[j] = tempm;
                    float temps = score_st[i];
                    score_st[i] = score_st[j];
                    score_st[j] = temps;
                }
            }
        }

        /*reduce*/
        PROCESSOR::Sum(score_st,sum_score_st,n_root_moves);

        MOVE l_bmove = bmove;
        if(PROCESSOR::host_id == 0) {
            float bests = -1;
            for(int i = 0; i < n_root_moves; i++) {
                if(sum_score_st[i] > bests) {
                    bests = sum_score_st[i];
                    bmove = move_st[i];
                }
            }
        }

        /*sort by score*/
        for(int i = 0;i < n_root_moves; i++) {
            for(int j = i + 1;j < n_root_moves;j++) {
                if(score_st[i] < score_st[j]) {
                    MOVE tempm = move_st[i];
                    move_st[i] = move_st[j];
                    move_st[j] = tempm;
                    float temps = score_st[i];
                    score_st[i] = score_st[j];
                    score_st[j] = temps;
                    if(PROCESSOR::host_id == 0) {
                        float temps = sum_score_st[i];
                        sum_score_st[i] = sum_score_st[j];
                        sum_score_st[j] = temps;
                    }
                }
            }
        }

        /*print best 5 moves vote weights*/
        char info[128];
        sprintf(info,"# [%d] vote:",PROCESSOR::host_id);
        for(int i = 0; i < n_root_moves && i < 8; i++) {
            char mv_str[16], str[32];
            mov_str(move_st[i],mv_str);
            sprintf(str, "%5s %5.3f", mv_str, score_st[i]);
            strcat(info,str);
        }
        strcat(info,"\n");

        /*gather all voting info and print it*/
        char* info_all = 0;
        if(PROCESSOR::host_id == 0)
            info_all = (char*)malloc(PROCESSOR::n_hosts * 128);
        PROCESSOR::Gather(info, info_all, strlen(info)+1, 128);
        if(PROCESSOR::host_id == 0) {
            for(int i = 0; i < PROCESSOR::n_hosts; i++)
                print(info_all + 128 * i);
            free(info_all);
        }

        /*print voted best move*/
        if(PROCESSOR::host_id == 0) {
            for(int i = 0;i < n_root_moves; i++) {
                for(int j = i + 1;j < n_root_moves;j++) {
                    if(sum_score_st[i] < sum_score_st[j]) {
                        MOVE tempm = move_st[i];
                        move_st[i] = move_st[j];
                        move_st[j] = tempm;
                        float temps = sum_score_st[i];
                        sum_score_st[i] = sum_score_st[j];
                        sum_score_st[j] = temps;
                    }
                }
            }

            strcpy(info,"#     sum :");
            for(int i = 0; i < n_root_moves && i < 8; i++) {
                char mv_str[16], str[32];
                mov_str(move_st[i],mv_str);
                sprintf(str, "%5s %5.3f", mv_str, sum_score_st[i]);
                strcat(info,str);
            }
            strcat(info,"\n");
            print(info);

            char mv_str[16];
            mov_str(bmove,mv_str);
            if(bmove == l_bmove)
                print("# voted best move: %s\n",mv_str);
            else {
                char mv_str2[16];
                mov_str(l_bmove,mv_str2);
                print("# voted best move: %s dis-agreement with %s\n",
                    mv_str, mv_str2);
            }
        }
    }
#endif

    /*was this first search?*/
    first_search = false;

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
            print("stat01: %d " FMT64 " %d %d %d %s\n",time_used / 10,unsigned(playouts),
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
    if(!strcmp(command, "contempt")) {
        contempt = atoi(commands[command_num++]);
    } else if(!strcmp(command, "alphabeta_man_c")) {
        alphabeta_man_c = atoi(commands[command_num++]);
    } else if(!strcmp(command, "multipv")) {
        multipv = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "multipv_margin")) {
        multipv_margin = atoi(commands[command_num++]);
#ifdef TUNE
    } else if(!strcmp(command, "aspiration_window")) {
        aspiration_window = atoi(commands[command_num++]);
    } else if(!strcmp(command, "razor_margin")) {
        razor_margin = atoi(commands[command_num++]);
    } else if(!strcmp(command, "failhigh_margin")) {
        failhigh_margin = atoi(commands[command_num++]);
    } else if(!strcmp(command, "futility_margin")) {
        futility_margin = atoi(commands[command_num++]);
    } else if(!strcmp(command, "probcut_margin")) {
        probcut_margin = atoi(commands[command_num++]);
    } else if(!strncmp(command, "lmp_count",9)) {
        lmp_count[atoi(&command[9])] = atoi(commands[command_num++]);
    } else if(!strncmp(command, "lmr_count",9)) {
        int* lmr = &lmr_count[0][0];
        lmr[atoi(&command[9])] = atoi(commands[command_num++]);
    } else if(!strncmp(command, "lmr_ntype_count",15)) {
        lmr_ntype_count[atoi(&command[15])] = atoi(commands[command_num++]);
#endif
    } else if(!strcmp(command, "smp_type")) {
        command = commands[command_num++];
        if(!strcmp(command,"YBW")) use_abdada_smp = 0;
        else if(!strcmp(command,"ABDADA")) use_abdada_smp = 1;
        else use_abdada_smp = 2;
    } else if(!strcmp(command, "cluster_type")) {
        command = commands[command_num++];
#ifdef CLUSTER
        if(!strcmp(command,"YBW")) use_abdada_cluster = 0;
        else if(!strcmp(command,"ABDADA")) use_abdada_cluster = 1;
        else use_abdada_cluster = 2;
#endif
    } else if(!strcmp(command, "smp_depth")) {
        PROCESSOR::SMP_SPLIT_DEPTH = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "cluster_depth")) {
        CLUSTER_CODE(PROCESSOR::CLUSTER_SPLIT_DEPTH = atoi(commands[command_num]));
        command_num++;
    } else {
        return false;
    }
    return true;
}
void print_search_params() {
    static const char* parallelt[] = {"YBW", "ABDADA", "SHT"};
    print_combo("smp_type",parallelt,use_abdada_smp,3);
    print_spin("smp_depth",PROCESSOR::SMP_SPLIT_DEPTH,1,10);
    CLUSTER_CODE(print_combo("cluster_type",parallelt,use_abdada_cluster,3));
    CLUSTER_CODE(print_spin("cluster_depth",PROCESSOR::CLUSTER_SPLIT_DEPTH,1,16));
    print_spin("contempt",contempt,0,100);
    print_spin("alphabeta_man_c",alphabeta_man_c,0,32);
    print_check("multipv",multipv);
    print_spin("multipv_margin",multipv_margin,0,1000);
}
