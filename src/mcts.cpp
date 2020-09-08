#include <random>
#include "scorpio.h"

/*mcts parameters*/
static unsigned int  cpuct_base = 19652;
static double  cpuct_init = 1.25;
static double  policy_temp = 2.35;
static double  cpuct_init_m = 1.25;
static double  policy_temp_m = 2.35;
static double  cpuct_init_e = 1.25;
static double  policy_temp_e = 2.35;
static double  fpu_red = 0.33;
static int     fpu_is_loss = 0;
static double  fpu_red_m = 0.33;
static int     fpu_is_loss_m = 0;
static double  fpu_red_e = 0.33;
static int     fpu_is_loss_e = 0;
static int reuse_tree = 1;
static int  backup_type_setting = MIX_VISIT;
static int  backup_type = backup_type_setting;
static double frac_alphabeta = 0.0; 
static double frac_freeze_tree = 1.0;
static double frac_abrollouts = 0.2;
static int  alphabeta_depth = 1;
static int  evaluate_depth = 0;
static int virtual_loss = 1;
static unsigned int visit_threshold = 800;
static std::random_device rd;
static std::mt19937 mtgen(rd());
static double noise_frac = 0.25;
static double noise_alpha = 0.3;
static double noise_beta = 1.0;
static int temp_plies = 30;
static double rand_temp = 1.0;
static double rand_temp_end = 0.0;
static const int low_visits_threshold = 100;
static const int node_size = 
    (sizeof(Node) + 32 * (sizeof(MOVE) + sizeof(float)));
static float min_policy_value = 1.0 / 100;
static int playout_cap_rand = 1;
static float frac_full_playouts = 0.25;
static float frac_sv_low = 1.0 / 3;
static float pcr_write_low = 0;
static float kld_threshold = 0;
static int early_stop = 1;
static int sp_resign_value = 600;
static int forced_playouts = 0;
static int policy_pruning = 0;
int  mcts_strategy_depth = 30;
int train_data_type = 0;

int montecarlo = 0;
int rollout_type = ALPHABETA;
bool freeze_tree = false;
double frac_abprior = 0.3;

int ensemble = 0;
static float ensemble_setting = 0;
int ensemble_type = 0;
VOLATILE int turn_off_ensemble = 0;
static VOLATILE int n_terminal = 0;

/*Nodes and edges of tree*/
std::vector<Node*> Node::mem_[MAX_CPUS];
std::map<int, std::vector<int*> > Edges::mem_[MAX_CPUS];
VOLATILE unsigned int Node::total_nodes = 0;
unsigned int Node::max_tree_nodes = 0;
unsigned int Node::max_tree_depth = 0;

Node* Node::allocate(int id) {
    static const int MEM_INC = 1024;
    Node* n;
    
    if(mem_[id].empty()) {
        n = new Node[MEM_INC];
        mem_[id].reserve(MEM_INC);
        for(int i = 0;i < MEM_INC;i++)
            mem_[id].push_back(&n[i]);
    }
    n = mem_[id].back();
    mem_[id].pop_back();
    l_add(total_nodes,1);

    n->clear();
    return n;
}

void Node::reclaim(Node* n, int id) {
    Node* current = n->child;
    while(current) {
        if(current->is_dead());
        else if(!current->child) {
            Edges::reclaim(current->edges,id);
            mem_[id].push_back(current);
            l_add(total_nodes,-1);
        } else
            reclaim(current,id);
        current = current->next;
    }
    Edges::reclaim(n->edges,id);
    mem_[id].push_back(n);
    l_add(total_nodes,-1);
}

void Edges::allocate(Edges& edges, int id, int sz) {
    if(mem_[id].count(sz) > 0) {
        std::vector<int*>& vec = mem_[id][sz];
        if(!vec.empty()) {
            edges._data = vec.back();
            vec.pop_back();
            return;
        }
    }
    size_t bytes = sz * (sizeof(MOVE) + sizeof(int));
    edges._data = (int*) malloc(bytes);
}

void Edges::reclaim(Edges& edges, int id) {
    std::vector<int*>& vec = mem_[id][edges.count];
    vec.push_back(edges._data);
}

static void add_node(Node* n, Node* node) {
    if(!n->child)
        n->child = node;
    else {
        Node* current = n->child, *prev;
        while(current) {
            prev = current;
            current = current->next;
        }
        prev->next = node;
    }
}

Node* Node::add_child(int processor_id, int idx, 
    MOVE move, float policy, float score
    ) {
    Node* node = Node::allocate(processor_id);
    node->move = move;
    node->score = score;
    node->visits = 0;
    node->alpha = -MATE_SCORE;
    if(rollout_type == ALPHABETA)
        node->beta = MATE_SCORE;
    else
        node->pre_noise_policy = policy;
    node->rank = idx + 1;
    node->prior = node->score;
    node->policy = policy;
    add_node(this,node);
    return node;
}

Node* Node::add_null_child(int processor_id, float score) {
    Node* node = Node::allocate(processor_id);
    node->move = 0;
    node->score = score;
    node->visits = 0;
    node->alpha = -MATE_SCORE;
    node->beta = MATE_SCORE;
    node->rank = 0;
    node->prior = node->score;
    node->policy = 0;
    add_node(this,node);
    return node;
}

void Node::rank_children(Node* n) {

    /*rank all children*/
    Node* current = n->child, *best = 0;
    int brank = MAX_MOVES - 1;

    while(current) {
        /*rank subtree first*/
        if(!current->is_dead())
            rank_children(current);

        /*rank current node*/
        if(current->move) {
            double val = -current->score;
            if(current->is_pvmove())
                val += MAX_HIST;

            /*find rank of current child*/
            int rank = 1;
            Node* cur = n->child;
            while(cur) {
                if(cur->move && cur != current) {
                    double val1 = -cur->score;
                    if(cur->is_pvmove())
                        val1 += MAX_HIST;

                    if(val1 > val ||
                        (val1 == val && cur->rank < current->rank) ||
                        (val1 == val && cur->rank == current->rank &&
                         cur->visits > current->visits)
                        ) rank++;
                }
                cur = cur->next;
            }
            current->rank = rank;

            /*best child*/
            if(rank < brank) {
                brank = rank;
                best = current;
            }
        } else {
            current->rank = 0;
        }

        current = current->next;
    }

    /*ensure one child has rank 1*/
    if(best)
        best->rank = 1;
}

void Node::reset_bounds(Node* n) {
    Node* current = n->child;
    while(current) {
        current->alpha = -n->beta;
        current->beta = -n->alpha;
        if(!current->is_dead())
            reset_bounds(current);
        current->flag = 0;
        current->busy = 0;
        current = current->next;
    }
}

void Node::split(Node* n, std::vector<Node*>* pn, const int S, int& T) {
    static int id = 0;
    Node* current = n->child;
    while(current) {
        if(current->visits <= (unsigned)S || !current->child) {
            pn[id].push_back(current);
            current->set_dead();

            T += current->visits;
            if(T >= S) {
                T = 0;
                if(++id >= PROCESSOR::n_processors) id = 0;
            }
        } else {
            split(current,pn,S,T);
        }
        current = current->next;
    }
}

Node* Node::Max_UCB_select(Node* n, bool has_ab, bool is_root, int processor_id) {
    double uct, fpu, bvalue = -10;
    double dCPUCT = cpuct_init + log((n->visits + cpuct_base + 1.0) / cpuct_base);
    double factor = dCPUCT * sqrt(double(n->visits));
    Node* current, *bnode = 0;
    unsigned vst, vvst = 0;

    if(is_root || n->visits > 10000)
        fpu = 1.0;            //fpu = win
    else if(!fpu_is_loss) {
        float fpur = fpu_red; //fpu = reduction
        if(n->visits > 3200)
            fpur = 0;         //fpu reduction = 0 
        else {
            fpu = 0;
            current = n->child;
            while(current) {
                if(current->visits && current->move)
                    fpu += current->policy;
                current = current->next;
            }
        }
        fpu = logistic(n->score) - fpur * sqrt(fpu);
    } else {
        fpu = (1 - fpu_is_loss) / 2.0; //fpu is loss or win
    }

    current = n->child;
    while(current) {
        if(current->move && !current->is_dead()) {

            vst = current->visits;
#ifdef PARALLEL
            vvst = virtual_loss * current->get_busy();
            if(current->is_create()) vvst <<= 2;
            vst += vvst;
#endif
            if(!current->visits) {
                uct = fpu;
            } else {
                uct = logistic(-current->score);
                uct += (-uct * vvst) / (vst + 1);
            }
            
            if(has_ab) {
                double uctp = logistic(-current->prior);
                uct = 0.5 * ((1 - frac_abprior) * uct + 
                            frac_abprior * uctp + 
                            MIN(uct,uctp));
            }

            uct += current->policy * factor / (vst + 1);

#if 1
            /*forced playouts*/
            if(forced_playouts && is_selfplay && is_root && current->visits > 0) {
                unsigned int n_forced = sqrt(2 * current->policy * n->visits);
                if(current->visits < n_forced) uct += 5;
            }
#endif

            if(uct > bvalue) {
                bvalue = uct;
                bnode = current;
            }

        }
        current = current->next;
    }

    /*check edges and add child*/
    if(n->edges.try_create()) {
        int idx = n->edges.get_children();
        if(idx < n->edges.count) {
            uct = fpu;
            uct += n->edges.scores()[idx] * factor;
            if(uct > bvalue) {
                bnode = n->add_child(processor_id, idx,
                    n->edges.moves()[idx],
                    n->edges.scores()[idx],
                    -n->edges.score);
                n->edges.inc_children();
                n->edges.clear_create();
                return bnode;
            }
        }
        n->edges.clear_create();
    }

    return bnode;
}
Node* Node::Max_AB_select(Node* n, int alpha, int beta, bool try_null,
    bool search_by_rank, int processor_id
    ) {
    double bvalue = -MAX_NUMBER, uct;
    Node* current, *bnode = 0;
    int alphac, betac;

    /*lock*/
    while(!n->edges.try_create())
        t_pause();

    current = n->child;
    while(current) {
        alphac = current->alpha;
        betac = current->beta;
        if(alpha > alphac) alphac = alpha;
        if(beta  < betac)   betac = beta;

        if(alphac < betac) {
            /*base score*/
            uct = -current->score;
            
            /*nullmove score*/
            if(!current->move) {
                if(try_null) uct = MATE_SCORE;
                else uct = -MATE_SCORE;
            }
            /*pv node*/
            else if(search_by_rank) {
                uct = MAX_MOVES - current->rank;
                if(current->rank == 1) uct = MATE_SCORE;
            }
#ifdef PARALLEL
            /*Discourage selection of busy node*/
            uct -= virtual_loss * current->get_busy() * 10;
#endif
            /*pick best*/
            if(uct > bvalue) {
                bvalue = uct;
                bnode = current;
            }
        }

        current = current->next;
    }

    /*unlock*/
    n->edges.clear_create();

    /*check edges and add child*/
    uct = -n->edges.score;
    if(search_by_rank)
        uct = MAX_MOVES - n->edges.n_children;

    if(uct > bvalue) {
        if(n->edges.try_create()) {
            int idx = n->edges.get_children();
            if(idx < n->edges.count) {
                bnode = n->add_child(processor_id, idx,
                    n->edges.moves()[idx],
                    n->edges.scores()[idx],
                    -n->edges.score);
                n->edges.inc_children();
                n->edges.clear_create();
                return bnode;
            }
            n->edges.clear_create();
        }
    }

    return bnode;
}

Node* Node::Random_select(Node* n, int hply) {
    Node* current, *bnode = n->child;
    int count;
    double val;
    std::vector<Node*> node_pt;
    std::vector<int> freq;

    count = 0;
    current = n->child;
    while(current) {
        if(current->move) {
            node_pt.push_back(current);
            val = current->visits;
            if(n->visits < low_visits_threshold) 
                val += (low_visits_threshold - n->visits) * current->policy;
            else
                val++;
            if(hply < temp_plies)
                val = pow(val, 1.0 / rand_temp);
            else
                val = pow(val, 1.0 / rand_temp_end);
            freq.push_back(1000 * val);
            count++;
        }
        current = current->next;
    }
    if(count) {
        std::discrete_distribution<int> dist(freq.begin(),freq.end());
        int indexc = dist(mtgen);
        bnode = node_pt[indexc];
#if 0
        for(int i = 0; i < count; ++i) {
            print("%c%d. %d\n",(i == indexc) ? '*':' ',i,freq[i]);
        }
#endif
    }

    return bnode;
}

Node* Node::Best_select(Node* n, bool has_ab) {
    double bvalue = -MAX_NUMBER;
    Node* current = n->child, *bnode = current;

    while(current) {
        if(current->move) {
            if(rollout_type == MCTS ||
                (rollout_type == ALPHABETA && 
                /* must be finished or ranked first */
                (current->alpha >= current->beta ||
                 current->rank == 1)) 
            ) {
                double val;
                if(rollout_type == MCTS &&
                    !( backup_type == MINMAX ||
                      backup_type == MINMAX_MEM )
                ) {
                    val = current->visits;
                    if(n->visits < low_visits_threshold) 
                        val += (low_visits_threshold - n->visits) * current->policy;
                    else
                        val++;
                } else {
                    if(current->visits)
                        val = logistic(-current->score);
                    else
                        val = bvalue - 1;

                    if(has_ab && (rollout_type == MCTS)) {
                        double valp = logistic(-current->prior);
                        val = 0.5 * ((1 - frac_abprior) * val + 
                                    frac_abprior * valp + 
                                    MIN(val,valp));
                    }
                }

                if(val > bvalue || (val == bvalue 
                    && current->rank < bnode->rank)) {
                    bvalue = val;
                    bnode = current;
                }
            }

        }
        current = current->next;
    }

    return bnode;
}
float Node::Min_score(Node* n) {
    Node* current = n->child, *bnode = current;
    while(current) {
        if(current->move && current->visits > 0) {
            if(current->score < bnode->score)
                bnode = current;
        }
        current = current->next;
    }
    return bnode->score;
}
float Node::Max_visits_score(Node* n) {
    Node* current = n->child, *bnode = current;
    while(current) {
        if(current->move && current->visits > 0) {
            if(current->visits > bnode->visits)
                bnode = current;
        }
        current = current->next;
    }
    return bnode->score;
}
float Node::Avg_score(Node* n) {
    double tvalue = 0;
    unsigned int tvisits = 0;
    Node* current = n->child;
    while(current) {
        if(current->move && current->visits > 0) {
            tvalue += logistic(current->score) * current->visits;
            tvisits += current->visits;
        }
        current = current->next;
    }
    if(tvisits == 0) {
        tvalue = logistic(-n->score);
        tvisits = 1;
    }
    return logit(tvalue / tvisits);
}
float Node::Avg_score_mem(Node* n, double score, int visits) {
    double sc = logistic(n->score);
    double sc1 = logistic(score);
    sc += (sc1 - sc) * visits / (n->visits + visits);
    return logit(sc);
}
void Node::Backup(Node* n,double& score,int visits) {
    if(rollout_type == MCTS) {
        double pscore = score;
        /*Compute parent's score from children*/
        if(backup_type == MIX_VISIT) {
            if(n->visits > visit_threshold)
                score = -Max_visits_score(n);
            else
                score = -Avg_score(n);
        } 
        else if(backup_type == CLASSIC)
            score = Avg_score_mem(n,score,visits);
        else if(backup_type == AVERAGE)
            score = -Avg_score(n);
        else {
            if(backup_type == MINMAX || backup_type == MINMAX_MEM)
                score = -Min_score(n);
            else if(backup_type == MIX  || backup_type == MIX_MEM)
                score = -(Min_score(n) + 3 * Avg_score(n)) / 4;

            if(backup_type >= MINMAX_MEM)
                score = Avg_score_mem(n,score,visits);
        }
        n->update_score(score);
        if(backup_type == CLASSIC)
            score = pscore;
    } else {

        /*lock*/
        while(!n->edges.try_create())
            t_pause();

        score = -Min_score(n);
        n->update_score(score);

        /*Update alpha-beta bounds. Note: alpha is updated only from 
          child just searched (next), beta is updated from remaining 
          unsearched children */
        
        if(n->alpha < n->beta) {

            /*nodes*/
            int alpha = -MATE_SCORE;
            int beta = -MATE_SCORE;
            int count = 0;
            Node* current = n->child;
            while(current) {
                if(current->move) {
                    if(-current->beta > alpha)
                        alpha = -current->beta;
                    if(-current->alpha > beta)
                        beta = -current->alpha;
                    count++;
                }
                current = current->next;
            }

            /*edges*/
            if(count < n->edges.count)
                beta = MATE_SCORE;

            n->set_bounds(alpha,beta);
        }

        /*unlock*/
        n->edges.clear_create();
    }
}
void Node::BackupLeaf(Node* n,double& score) {
    if(rollout_type == MCTS) {
        n->update_score(score);
    } else if(n->alpha < n->beta) {
        n->update_score(score);
        n->set_bounds(score,score);
    }
}

void SEARCHER::create_children(Node* n) {

    /*maximum tree depth*/
    if(ply > (int)Node::max_tree_depth)
        Node::max_tree_depth = ply;

    /*generate and score moves*/
    if(ply)
        generate_and_score_moves(evaluate_depth,-MATE_SCORE,MATE_SCORE);

    if(pstack->count)
        n->score = pstack->best_score;

    /*add edges tree*/
    n->edges.count = pstack->count;
    n->edges.score = n->score;
    if(pstack->count) {
        Edges::allocate(n->edges, processor_id, pstack->count);
        memcpy(n->edges.moves(), pstack->move_st,
            pstack->count * sizeof(MOVE));
        memcpy(n->edges.scores(), pstack->score_st,
            pstack->count * sizeof(int));
    }

    /*add only one child if not at the root*/
    int nleaf = ply ? MIN(1,pstack->count) : pstack->count;
    for(int i = 0; i < nleaf; i++) {
        float* pp =(float*)&pstack->score_st[i];
        n->add_child(processor_id, i,
            pstack->move_st[i], *pp, -n->score);
    }

    /*add nullmove*/
    if(use_nullmove
        && pstack->count
        && rollout_type != MCTS
        && ply > 0
        && !hstack[hply - 1].checks
        && piece_c[player]
        && hstack[hply - 1].move != 0
        ) {
        n->add_null_child(processor_id, -n->score);
    }

    /*set number of children now*/
    n->edges.n_children = nleaf;
}

void SEARCHER::handle_terminal(Node* n, bool is_terminal) {
    /*wait until collision limit is reached*/
    if(rollout_type == MCTS &&
        l_add(n_terminal,1) <= (PROCESSOR::n_processors >> 2) )
        ;
    else {
        /*we are about to do a useless NN call for the sake of 
        completing batch_size. Do something useful by moving to 
        next ply instead, and cache result.*/
        if(!is_terminal) {
            gen_all_legal();
            if(pstack->count > 0) {
                int idx = n->get_busy() - 2;
                if(idx >= pstack->count || idx < 0) idx = 0;
                PUSH_MOVE(pstack->move_st[idx]);
                gen_all_legal();
                if(pstack->count > 0) {
                    probe_neural(true);
                    POP_MOVE();
                    return;
                }
                POP_MOVE();
            }
        }
        /*Do useless hard probe without caching*/
        probe_neural(true);
    }
}

void SEARCHER::play_simulation(Node* n, double& score, int& visits) {

    nodes++;
    visits = 1;

    /*set busy flag*/
    n->inc_busy();

#if 0
    unsigned int nvisits = n->visits;
#endif

    /*Terminal node*/
    if(ply) {
        /*Draw*/
        if(draw()) {
            score = ((scorpio == player) ? -contempt : contempt);
            goto BACKUP_LEAF;
        /*bitbases*/
        } else if(bitbase_cutoff()) {
            score = pstack->best_score;
            goto BACKUP_LEAF;
        /*Reached max plies and depth*/
        } else if(ply >= MAX_PLY - 1 || pstack->depth <= 0) {
            score = n->score;
            goto BACKUP_LEAF;
        /*mate distance pruning*/
        } else if(rollout_type == ALPHABETA 
            && n->alpha > MATE_SCORE - WIN_PLY * (ply + 1)) {
            score = n->alpha;
            goto BACKUP_LEAF;
        }
    }

    /*No children*/
    if(!n->edges.n_children) {

        /*run out of memory for nodes*/
        if(rollout_type == MCTS
            && Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes
            ) {
            if(l_set(abort_search,1) == 0)
                print_info("Maximum number of nodes reached.\n");
            visits = 0;
            goto FINISH;
        } else if(rollout_type == ALPHABETA && 
             (
             Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes       
             || freeze_tree
             || pstack->depth <= alphabeta_depth * UNITDEPTH
             )
            ) {
            if(Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes &&
                !freeze_tree && processor_id == 0) {
                freeze_tree = true;
                print_info("Maximum number of nodes reached.\n");
            }
            score = get_search_score();
            if(stop_searcher || abort_search)
                goto FINISH;
        /*create children*/
        } else {

            if(n->try_create()) {
                if(!n->edges.n_children)
                    create_children(n);
                n->clear_create();
            } else {
                if(use_nn) {
                    handle_terminal(n,false);
                    visits = 0;
                } else
                    score = n->score;
                goto FINISH;
            }

            if(!n->edges.n_children) {
                if(hstack[hply - 1].checks)
                    score = -MATE_SCORE + WIN_PLY * (ply + 1);
                else 
                    score = ((scorpio == player) ? -contempt : contempt);
            } else {
                if(rollout_type == ALPHABETA) {
                    /*Expand more in case of AB*/
                    goto SELECT;
                } else  {
                    /*Backup now if MCTS*/
                    score = n->score;
                    goto FINISH;
                }
            }
        }

BACKUP_LEAF:
        Node::BackupLeaf(n,score);
        if(use_nn)
            handle_terminal(n,true);

    /*Has children*/
    } else {
SELECT:
        /*select move*/
        Node* next = 0;
        if(rollout_type == ALPHABETA) {
            bool try_null = pstack->node_type != PV_NODE
                            && pstack->depth >= 4 * UNITDEPTH 
                            && n->score >= pstack->beta;
            bool search_by_rank = (pstack->node_type == PV_NODE);

            next = Node::Max_AB_select(n,-pstack->beta,-pstack->alpha,
                try_null,search_by_rank, processor_id);
        } else {
            bool is_root = (n == root_node);
            bool has_ab = (is_root && frac_abprior > 0);
            next = Node::Max_UCB_select(n, has_ab, is_root, processor_id);
        }

        /*This could happen in parallel search*/
        if(!next) {
            score = n->score;
            goto FINISH;
        }

        /*Determine next node type*/
        int next_node_t;
        if(pstack->node_type == ALL_NODE) {
            next_node_t = CUT_NODE;
        } else if(pstack->node_type == CUT_NODE) {
            next_node_t = ALL_NODE;
        } else {
            if(next->rank == 1 || next->is_failed_scout())
                next_node_t = PV_NODE;
            else
                next_node_t = CUT_NODE;
        }

        /*Determine next alpha-beta bound*/
        int alphac, betac;
        alphac = -pstack->beta;
        betac = -pstack->alpha;
        if(next->alpha > alphac) alphac = next->alpha;
        if(next->beta < betac)    betac = next->beta;

        if(next->move) {
            bool try_scout = (alphac + 1 < betac &&
                              ABS(betac) != MATE_SCORE &&
                              pstack->node_type == PV_NODE && 
                              next_node_t == CUT_NODE);
            /*Make move*/
            PUSH_MOVE(next->move);
RESEARCH:
            if(try_scout) {
                pstack->alpha = betac - 1;
                pstack->beta = betac;
            } else {
                pstack->alpha = alphac;
                pstack->beta = betac;
            }
            pstack->node_type = next_node_t;
            pstack->depth = (pstack - 1)->depth - UNITDEPTH;
            pstack->search_state = NULL_MOVE;
            /*Next ply depth*/
            if(rollout_type == ALPHABETA) {
                if(use_selective 
                    && be_selective(next->rank,true)
                    && ABS(betac) != MATE_SCORE 
                    ) {
                    next->set_bounds(betac,betac);
                    POP_MOVE();
                    goto BACKUP;
                }
            }
            /*Simulate selected move*/
            play_simulation(next,score,visits);

            score = -score;
            if(stop_searcher || abort_search)
                goto FINISH;

            /*Research if necessary when window closes*/
            if(visits
                && rollout_type == ALPHABETA
                && next->alpha >= next->beta
                ) {
#if 0
                /*reduction research*/
                if(pstack->reduction
                    && next->alpha > (pstack - 1)->alpha
                    ) {
                    Node::reset_bounds(next,alphac,betac);
                    next->rank = 1;
                    goto RESEARCH;
                }
#endif
                /*scout research*/
                if(try_scout 
                    && score > (pstack - 1)->alpha
                    && score < (pstack - 1)->beta
                    ) {
                    alphac = -(pstack - 1)->beta;
                    betac = -(pstack - 1)->alpha;
                    if(next->try_failed_scout()) {
                        try_scout = false;
                        next_node_t = PV_NODE;
                        next->alpha = alphac;
                        next->beta = betac;
                        Node::reset_bounds(next);
                        goto RESEARCH;
                    } else {
                        next->set_bounds(alphac,betac);
                    }
                }
            }

            /*Undo move*/
            POP_MOVE();

            if(visits == 0)
                goto FINISH;
        } else {
            /*Make nullmove*/
            PUSH_NULL();
            pstack->extension = 0;
            pstack->reduction = 0;
            pstack->alpha = alphac;
            pstack->beta = alphac + 1;
            pstack->node_type = next_node_t;
            pstack->search_state = NORMAL_MOVE;
            /*Next ply depth*/
            pstack->depth = (pstack - 1)->depth - 3 * UNITDEPTH - 
                            (pstack - 1)->depth / 4 -
                            (MIN(3 , (n->score - (pstack - 1)->beta) / 128) * UNITDEPTH);
            /*Simulate nullmove*/
            play_simulation(next,score,visits);

            score = -score;
            if(stop_searcher || abort_search)
                goto FINISH;

            /*Undo nullmove*/
            POP_NULL();

            /*Nullmove cutoff*/
            if(visits && next->alpha >= next->beta) {
                if(score >= pstack->beta)
                    n->set_bounds(score,score);
            }
            goto FINISH;
            /*end null move*/
        }
BACKUP:
        /*Backup score and bounds*/
        Node::Backup(n,score,visits);

        if(rollout_type == ALPHABETA) {

            /*Update alpha for next sibling*/
            if(next->alpha >= next->beta) {
                if(n->alpha > pstack->alpha)
                    pstack->alpha = n->alpha;
            }
            
            /*Select move from this node again until windows closes.
              This is similar to what a standard alpha-beta searcher does.
              Currently, this is slower than the rollouts version. */
#if 0
            if(n->alpha < n->beta && pstack->alpha < pstack->beta &&
               n->beta > pstack->alpha && n->alpha < pstack->beta) {
                goto SELECT;
            }
            visits = n->visits - nvisits;
#endif
        }
    }

FINISH:
    n->update_visits(visits);
    n->dec_busy();
}

/*compute kld b/n network policy and current simulation pi*/
double SEARCHER::compute_kld() {
    Node* current = root_node->child;
    double kld = 0;
    while(current) {
        if(current->visits) {
            kld -= current->pre_noise_policy * 
                  log(current->visits / ((current->pre_noise_policy + 1e-6) * root_node->visits));
        }
        current = current->next;
    }
    return kld;
}

void SEARCHER::check_mcts_quit(bool single) {

    /*use kld threshold*/
    if(kld_threshold > 0) {
        double kld = compute_kld();
        double dkld = ABS(kld - prev_kld);
        prev_kld = kld;
#if 0
        print("KLD: %f DKLD: %f\n", kld, dkld);
#endif
        if(dkld <= kld_threshold) {
            if(single)
                stop_searcher = 1;
            else
                abort_search = 1;
            return;
        }
    }

    /*check if pruning is enabled*/
    if(!early_stop)
        return;

    /*find top two moves*/
    Node* current = root_node->child;
    unsigned int max_visits[2] = {0, 0};
    Node* bnval = current, *bnvis = current, *bnvis2 = current;
    while(current) {
        if(current->visits > max_visits[0]) {
            max_visits[1] = max_visits[0];
            max_visits[0] = current->visits;
            bnvis2 = bnvis;
            bnvis = current;
        } else if(current->visits > max_visits[1]) {
            max_visits[1] = current->visits;
            bnvis2 = current;
        }
        if(current->visits > 0) {
            if(-current->score > -bnval->score)
                bnval = current;
        }
        current = current->next;
    }

    /*determine time factor*/
    time_factor = 1.0;
    if(bnval != bnvis)
        time_factor *= 1.3;
    if(root_node->score <= -150)
        time_factor *= 3.0;
    else if(root_node->score <= -120)
        time_factor *= 2.3;
    else if(root_node->score <= -70)
        time_factor *= 1.6;
    else if(root_node->score <= -30)
        time_factor *= 1.3;
    else if(root_node->score >= 100)
        time_factor *= 2.0;
    else if(root_node->score >= 55)
        time_factor *= 1.5;
    else if(ABS(root_node->score - old_root_score) > 30)
        time_factor *= 1.3;
    else if(ABS(root_node->score) > 10)
        time_factor *= 1.1;

    int remain_visits;
    if(chess_clock.max_visits == MAX_NUMBER) {
        int time_used = MAX(1,get_time() - start_time);
        int remain_time = time_factor * chess_clock.search_time - time_used;
        remain_visits = (remain_time / (double)time_used) * root_node->visits;
    } else {
        remain_visits = chess_clock.max_visits - root_node->visits;
    }

    /*prune root nodes*/
    int visdiff;
    double factor;

    factor = sqrt(bnvis->policy / (bnvis2->policy + 1e-8));
    if(factor >= 20) factor = 20;

    visdiff = (bnvis->visits - bnvis2->visits) * (factor / 1.2);
    if(visdiff >= remain_visits) {
        if(single)
            stop_searcher = 1;
        else
            abort_search = 1;
    }

    if(!abort_search) {
        Node* current = root_node->child;
        while(current) {
            factor = sqrt(bnvis->policy / (current->policy + 1e-8));
            if(factor >= 20) factor = 20;

            visdiff = (bnvis->visits - current->visits) * (factor / 1.2);
            if(current->visits && !current->is_dead() && visdiff >= remain_visits) {
                current->set_dead();
#if 0
                char mvs[16];
                mov_str(current->move,mvs);
                print("Killing node: %s %d %d = %d %d\n",
                    mvs,current->visits,bnvis->visits, 
                    visdiff, remain_visits);
#endif
            }
            current = current->next;
        }
    }
}

void SEARCHER::search_mc(bool single, unsigned int nodes_limit) {
    Node* root = root_node;
    double pfrac = 0,score;
    int visits;
    int oalpha = pstack->alpha;
    int obeta = pstack->beta;
    unsigned int ovisits = root->visits;
    unsigned int visits_poll;

    /*poll input after this many playouts*/
    if(chess_clock.max_visits != MAX_NUMBER) {
        if(is_selfplay) visits_poll = 100;
        else visits_poll = chess_clock.max_visits / 40;
    } else if(use_nn)
        visits_poll = 4 * PROCESSOR::n_processors;
    else
        visits_poll = MAX(200, average_pps / 40);
    prev_kld = -1;

    /*wait until all idle processors are awake*/
    if(!single) {
        static VOLATILE int t_count = 0;
        int p_t_count = l_add(t_count,1);
        if(p_t_count == PROCESSOR::n_processors - 1)
            t_count = 0;
        while(t_count > 0 && t_count < PROCESSOR::n_processors) {
            t_yield();
            if(SEARCHER::use_nn) t_sleep(SEARCHER::delay);
        }
    }

    /*Set alphabeta rollouts depth*/
    int ablimit = DEPTH((1 - frac_abrollouts) * pstack->depth);
    if(ablimit > alphabeta_depth)
        alphabeta_depth = ablimit;

    /*Current best move is ranked first*/
    Node* current = root->child, *best = current;
    while(current) {
        if(current->rank == 1) {
           best = current;
           break;
        }
        current = current->next;
    }

    /*do rollouts*/
    while(true) {
        /*fixed nodes*/
        if(chess_clock.max_visits != MAX_NUMBER
            && ( ((nodes_limit == 0) && root->visits >= (unsigned)chess_clock.max_visits) ||
                 ((nodes_limit != 0) && root->visits >= nodes_limit) )
            ) {
            if(!single) abort_search = 1;
            break;
        }

        /*simulate*/
        play_simulation(root,score,visits);

        /*search stopped*/
        if(abort_search || stop_searcher)
            break;
        
        /*check for exit conditions*/
        if(rollout_type == ALPHABETA) {

            /*best move failed low*/
            if(best->alpha >= best->beta
                && -best->score <= oalpha
                ) {
                root_failed_low = 3;
                break;
            }

            /*exit when window closes*/
            if((root->alpha >= root->beta || 
                 root->alpha >= obeta ||
                 root->beta  <= oalpha)
                ) {
                break;
            }
        }

        /*threads searching different trees*/
        if(single) {
            if(root->visits - ovisits >= visits_poll) {
                check_mcts_quit(single);
                ovisits = root->visits;
            }
        /*all threads searching same tree*/
        } else if(processor_id == 0) {

            /*check for messages from other hosts*/
#ifdef CLUSTER
#   ifndef THREAD_POLLING
            if(root->visits - ovisits >= visits_poll) {
                ovisits = root->visits;
                processors[processor_id]->idle_loop();
            }
#   endif
#endif  
            /*rank 0*/
            if(true CLUSTER_CODE(&& PROCESSOR::host_id == 0)) { 
                /*check quit*/
                if(root->visits - ovisits >= visits_poll) {
                    ovisits = root->visits;
                    check_quit();

                    double frac = 1;
                    if(chess_clock.max_visits != MAX_NUMBER)
                        frac = double(root->visits) / chess_clock.max_visits;
                    else 
                        frac = double(get_time() - start_time) / chess_clock.search_time;

                    if(frac - pfrac >= 0.1) {
                        pfrac = frac;
                        if(rollout_type == MCTS) {
                            extract_pv(root);
                            root_score = root->score;
                            print_pv(root_score);
                            search_depth++;
                        }
                    }
                    
                    if((is_selfplay || (frac >= 0.1)) && !chess_clock.infinite_mode)
                        check_mcts_quit(single);

                    if(frac > ensemble_setting)
                        turn_off_ensemble = 1;

                    /*stop growing tree after some time*/
                    if(rollout_type == ALPHABETA && !freeze_tree && frac_freeze_tree < 1.0 &&
                        frac >= frac_freeze_tree * frac_alphabeta) {
                        freeze_tree = true;
                        print_info("Freezing tree.\n");
                    }
                    /*Switching rollouts type*/
                    if(rollout_type == ALPHABETA && frac_alphabeta != 1.0 
                        && frac > frac_alphabeta) {
                        print_info("Switching rollout type to MCTS.\n");
                        rollout_type = MCTS;
                        use_nn = save_use_nn;
                        search_depth = search_depth + mcts_strategy_depth;
                        pstack->depth = search_depth * UNITDEPTH;
                        root_failed_low = 0;
                        freeze_tree = false;
                    }
                }
#ifdef CLUSTER
                /*quit hosts*/
                if(abort_search)
                    PROCESSOR::quit_hosts();
#endif
            }
        }
    }

    /*update statistics of parent*/
#ifdef PARALLEL
    if(!single && master) {
        l_lock(lock_smp);
        l_lock(master->lock);
        update_master(1);
        l_unlock(master->lock);
        l_unlock(lock_smp);
    } else
#endif
    if(!single && !abort_search && !stop_searcher) {
        bool failed = (-best->score <= oalpha) || 
                      (-best->score >= obeta);
        if(!failed) {
            bool has_ab = (frac_abprior > 0);
            best = Node::Best_select(root,has_ab);
        }

        root->score = -best->score;
        root_score = root->score;
        pstack->best_score = root_score;

        best->score = -MATE_SCORE;
        extract_pv(root);
        best->score = -root_score;

        if(!failed)     
            print_pv(root_score);
    } else if(rollout_type == MCTS) {
        /*Search is aborted, print last pv*/
        for (int j = ply; j > 0 ; j--) {
            MOVE move = hstack[hply - 1].move;
            if(move) POP_MOVE();
            else POP_NULL();
        }
        /*Random selection for self play*/
        extract_pv(root,(is_selfplay && (hply < temp_plies || rand_temp_end > 0)));
        root_score = root->score;
        if(!single)
            print_pv(root_score);
        search_depth++;
    }
}
/*
Traverse tree in parallel
*/
static std::vector<Node*> gc[MAX_CPUS];

static void CDECL gc_thread_proc(void* seid_) {
    int* seid = (int*)seid_;
    for(int proc_id = seid[0]; proc_id < seid[1]; proc_id++) {
        for(unsigned int i = 0; i < gc[proc_id].size(); i++) {
            Node::reclaim(gc[proc_id][i],proc_id);
        }
    }
}
static void CDECL rank_reset_thread_proc(void* seid_) {
    int* seid = (int*)seid_;
    for(int proc_id = seid[0]; proc_id < seid[1]; proc_id++) {
        for(unsigned int i = 0; i < gc[proc_id].size(); i++) {
            Node::rank_children(gc[proc_id][i]);
            Node::reset_bounds(gc[proc_id][i]);
        }
    }
}

void Node::parallel_reclaim(Node* n) {
    int ncores = PROCESSOR::n_cores;
    int nprocs = PROCESSOR::n_processors;
    int T = 0, S = MAX(1,n->visits / (8 * nprocs)),
                 V = nprocs / ncores;

#ifdef PARALLEL
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        processors[i]->state = PARK;
#endif

    Node::split(n, gc, S, T);

    gc[0].push_back(n);

    int* seid = new int[2 * ncores];
    pthread_t* tid = new pthread_t[ncores];

    for(int i = 0; i < ncores; i++) {
        seid[2*i+0] = i * V;
        seid[2*i+1] = (i == ncores - 1) ? nprocs : ((i + 1) * V);
        t_create(tid[i],gc_thread_proc,&seid[2*i]);
    }
    for(int i = 0; i < ncores; i++)
        t_join(tid[i]);

    delete[] seid;
    delete[] tid;

    for(int i = 0; i < nprocs;i++)
        gc[i].clear();

#ifdef PARALLEL
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        processors[i]->state = WAIT;
#endif

}
void Node::parallel_rank_reset(Node* n) {
    int ncores = PROCESSOR::n_cores;
    int nprocs = PROCESSOR::n_processors;
    int T = 0, S = MAX(1,n->visits / (8 * nprocs)),
                 V = nprocs / ncores;

#ifdef PARALLEL
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        processors[i]->state = PARK;
#endif

    Node::split(n, gc, S, T);

    Node::rank_children(n);
    Node::reset_bounds(n);

    int* seid = new int[2 * ncores];
    pthread_t* tid = new pthread_t[ncores];

    for(int i = 0; i < ncores; i++) {
        seid[2*i+0] = i * V;
        seid[2*i+1] = (i == ncores - 1) ? nprocs : ((i + 1) * V);
        t_create(tid[i],rank_reset_thread_proc,&seid[2*i]);
    }
    for(int i = 0; i < ncores; i++)
        t_join(tid[i]);

    delete[] seid;
    delete[] tid;

    for(int i = 0; i < nprocs;i++)
        gc[i].clear();

#ifdef PARALLEL
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        processors[i]->state = WAIT;
#endif

}

/*
Manage search tree
*/
void SEARCHER::manage_tree(bool single) {
    if(root_node) {

        unsigned int s_total_nodes = Node::total_nodes;

        /*find root node*/
        int idx = 0;
        bool found = false;
        for( ;idx < 8 && hply >= (idx + 0); idx++) {
            if(hply >= (idx + 0) &&
                hstack[hply - (idx + 0)].hash_key == root_key) {
                found = true;
                break;
            }
        }
        
        /*Recycle nodes in parallel*/
        int st = get_time();

        if(idx == 0) {
            /*same position searched again*/
        } else if(found && reuse_tree) {
            MOVE move;

            Node* oroot = root_node, *new_root = 0;
            for(int j = (idx - 1); j >= 0; --j) {
                move = hstack[hply - 1 - j].move;

                Node* current = root_node->child, *prev = 0;
                while(current) {
                    if(current->move == move) {
                        if(j == 0) {
                            new_root = current;
                            if(current == root_node->child)
                                root_node->child = current->next;
                            else
                                prev->next = current->next;
                            current->next = 0;
                        }
                        root_node = current;
                        break;
                    }
                    prev = current;
                    current = current->next;
                }
                if(!current) break;
            }

            if(single) Node::reclaim(oroot,processor_id);
            else Node::parallel_reclaim(oroot);
            if(new_root) {
                root_node = new_root;

                Node* n = root_node;
                for(int i = n->edges.n_children; i < n->edges.count; i++) {
                    n->add_child(processor_id, i,
                        n->edges.moves()[i],
                        n->edges.scores()[i],
                        -n->edges.score);
                    n->edges.inc_children();
                }
            }
            else
                root_node = 0;
        } else {
            if(single) Node::reclaim(root_node,processor_id);
            else Node::parallel_reclaim(root_node);
            root_node = 0;
        }

        int en = get_time();

        /*print mem stat*/
        unsigned int tot = 0;
        for(int i = 0;i < PROCESSOR::n_processors;i++) {
#if 0
            print_info("Proc %d: %d\n",i,Node::mem_[i].size());
#endif
            tot += Node::mem_[i].size();
        }

        if(pv_print_style == 0) {
            print_info("Reclaimed %d nodes in %dms\n",(s_total_nodes - Node::total_nodes), en-st);
            print_info("Memory for mcts nodes: %.1fMB unused + %.1fMB intree = %.1fMB of %.1fMB total\n", 
                (double(tot) / (1024 * 1024)) * node_size, 
                (double(Node::total_nodes) / (1024 * 1024)) * node_size,
                (double(Node::total_nodes+tot) / (1024 * 1024)) * node_size,
                (double(Node::max_tree_nodes) / (1024 * 1024)) * node_size
                );
        }
    }

    if(!root_node) {
        print_log("# [Tree-not-found]\n");
        root_node = Node::allocate(processor_id);
    } else {
        print_log("# [Tree-found : visits %d score %d]\n",
            root_node->visits,int(root_node->score));

        /*remove null moves from root*/
        Node* current = root_node->child, *prev;
        while(current) {
            prev = current;
            if(current) current->clear_dead();
            current = current->next;
            if(current && current->move == 0) {
                prev->next = current->next;
                Node::reclaim(current,0);
            }
        }
    }
    if(!root_node->child) {
        create_children(root_node);
        root_node->visits++;
    }
    root_node->set_pvmove();
    root_key = hash_key;

    /*only have root child*/
    if(!freeze_tree && frac_freeze_tree == 0)
        freeze_tree = true;
    if(frac_alphabeta == 0) {
        rollout_type = MCTS;
        search_depth = MIN(search_depth + mcts_strategy_depth, MAX_PLY - 2);
    } else {
        rollout_type = ALPHABETA;
    }

    /*backup type*/
    backup_type = backup_type_setting;

    /*Dirichlet noise*/
    if(is_selfplay && (chess_clock.max_visits > 2 * low_visits_threshold) ) {
        const float alpha = noise_alpha, beta = noise_beta, frac = noise_frac;
        std::vector<double> noise;
        std::gamma_distribution<double> dist(alpha,beta);
        Node* current;

        double total = 0;
        current = root_node->child;
        while(current) {
            double n = dist(mtgen);
            noise.push_back(n);
            total += n;
            current = current->next;
        }
        if(total < 1e-8) total = 1e-8;

        int idx = 0;
        current = root_node->child;
        while(current) {
            double n = noise[idx++] / total;
#if 0
            char mvstr[16];
            mov_str(current->move,mvstr);
            print("%3d. %7s %8.2f %8.2f = %8.2f\n",
                current->rank,mvstr,
                current->policy,n,
                current->policy * (1 - frac) + n * frac);
#endif
            current->policy = current->policy * (1 - frac) + n * frac;
            current = current->next;
        }
    }
}
/*
Generate all moves
*/
void SEARCHER::generate_and_score_moves(int depth, int alpha, int beta) {

    /*generate moves here*/
    gen_all_legal();

    /*compute move probabilities*/
    if(pstack->count) {
        if(!use_nn) {
            bool save = skip_nn;
            skip_nn = true;
            evaluate_moves(depth,alpha,beta);
            skip_nn = save;

            pstack->best_score = pstack->score_st[0];

            if(montecarlo) {

                /*flat policy*/
                if(policy_temp >= 10.0) {
                    for(int i = 0;i < pstack->count; i++) {
                        float* p = (float*)&pstack->score_st[i];
                        *p = 1.0 / pstack->count;
                    }
                    return;
                }

                /*normalize policy*/
                double total = 0.f;
                for(int i = 0;i < pstack->count; i++) {
                    float p = logistic(pstack->score_st[i]);
                    p = exp(p * 10 / policy_temp);
                    total += p;
                }
                for(int i = 0;i < pstack->count; i++) {
                    float p = logistic(pstack->score_st[i]);
                    p = exp(p * 10 / policy_temp);
                    float* pp = (float*)&pstack->score_st[i];
                    *pp = p / total;
                }
            }
        } else {
            pstack->best_score = probe_neural();
            n_terminal = 0;

            /*flat policy*/
            if(policy_temp >= 10.0) {
                for(int i = 0;i < pstack->count; i++) {
                    float* p = (float*)&pstack->score_st[i];
                    *p = 1.0 / pstack->count;
                }
                return;
            }

            /*find minimum and maximum policy values*/
            double total = 0.f, maxp = -100, minp = 100;
            for(int i = 0;i < pstack->count; i++) {
                float* p = (float*)&pstack->score_st[i];
                MOVE& move = pstack->move_st[i];
                if(is_prom(move)) {
                    switch(PIECE(m_promote(move))) {
                        case queen:
                            break;
                        case rook:
                            *p = *p * 0.5;
                            break;
                        case knight:
                        case bishop:
                            *p = *p * 0.25;
                            break;
                    }
                }
                if(*p > maxp) maxp = *p;
                if(*p < minp) minp = *p;
            }

            /*Minimize draws for low visits training*/
            if(!ply && chess_clock.max_visits < low_visits_threshold) {
                int score = pstack->best_score;
                for(int i = 0;i < pstack->count; i++) {
                    float* p = (float*)&pstack->score_st[i];
                    MOVE& move = pstack->move_st[i];
                    int sfifty = fifty;

                    PUSH_MOVE(move);
                    gen_all_legal();
                    if(!pstack->count) {
                        if(hstack[hply - 1].checks) {      //mate
                            *p = 5 * maxp;
                        } else if(score >= -100) {         //stale-mate
                            *p = minp;
                        }
                    } else if(score >= -contempt && draw(1)) {   //repetition & 50-move draws
                        *p = minp;
                    } else if(sfifty > 0 && fifty == 0) {  //encourage progress
                        *p += sfifty * (maxp - minp) / 50;
                    }
                    POP_MOVE();
                }
            }
            /*normalize policy*/
            for(int i = 0;i < pstack->count; i++) {
                float* p = (float*)&pstack->score_st[i];
                total += exp( (*p - maxp) / policy_temp );
            }
            for(int i = 0;i < pstack->count; i++) {
                float* p = (float*)&pstack->score_st[i];
                float pp = exp( (*p - maxp) / policy_temp ) / total;  
                if(pp < 2 * min_policy_value)
                    pp = MAX(pp, min_policy_value + pp / 8);
                *p = pp;
            }

            for(int i = 0;i < pstack->count; i++)
                pstack->sort(i,pstack->count);
        }
    }
}
/*
* Self-play with policy
*/
static int work_type = 0;

static FILE* spfile = 0;
static FILE* spfile2 = 0;
static int spgames = 0;

/*training data structure*/
typedef struct TRAIN {
   int   nmoves;
   float value;
   int   bestm;
   int   moves[MAX_MOVES];
   float probs[MAX_MOVES];
   float scores[MAX_MOVES];
   char  fen[128];
} *PTRAIN;

/*multiple worker threads*/
void SEARCHER::launch_worker_threads() {

#ifdef PARALLEL
    /*attach helper processor here once*/
    l_lock(lock_smp);
    for(int i = 1;i < PROCESSOR::n_processors;i++) {
        attach_processor(i);
        processors[i]->state = GOSP;
    }
    l_unlock(lock_smp);

    /*montecarlo search*/
    t_sleep(30);
    worker_thread();

    /*wait till all helpers become idle*/
    idle_loop_main();
#else
    worker_thread();
#endif

}

/*selfplay with multiple threads*/
void SEARCHER::self_play_thread_all(FILE* fw, FILE* fw2, int ngames) {
    spfile = fw;
    spfile2 = fw2;
    spgames = ngames;
    work_type = 0;

    launch_worker_threads();
}

/*get training data from search*/
void SEARCHER::get_train_data(float& value, int& nmoves, int* moves, float* probs, float* scores, int& bestm) {
    Node* current;

    /*value*/
    value = logistic(root_node->score);
    if(player == black) 
        value = 1 - value;

    /*policy target pruning*/
    double factor, max_uct = 0;

    if(policy_pruning) {
        double dCPUCT = cpuct_init + log((root_node->visits + cpuct_base + 1.0) / cpuct_base), uct;
        factor = dCPUCT * sqrt(double(root_node->visits));

        current = root_node->child;
        while(current) {
            uct = logistic(-current->score);
            uct += current->policy * factor / (current->visits + 1);
#if 0
            char mvstr[16];
            mov_str(current->move,mvstr);
            print("%3d. %7s %9.2f %9.2f %9.4f\n",
                current->rank,mvstr,current->policy,
                logistic(-current->score),uct);
#endif
            if(uct > max_uct) max_uct = uct;
            current = current->next;
        }

#if 0
        print("PUCT(c*) = %5.2f c_puct*sqrt(N) = %5.2f\n",
                max_uct,factor);
        print(" NO     Move    Policy     Score   BeforeV    AfterV\n");
#endif
    }

    /*policy*/
    double val, total_visits = 0;
    int cnt = 0, diff = low_visits_threshold - root_node->visits;
    current = root_node->child;
    while(current) {
        /*skip underpromotions*/
        if(is_prom(current->move) && 
            PIECE(m_promote(current->move)) != queen) {
            current = current->next;
            continue;
        }
        /*probabilities*/
        if(train_data_type == 0 || train_data_type == 2) {
            val = current->visits;

            if(policy_pruning) {
                val = ((current->policy * factor) /
                    (max_uct - logistic(-current->score)) ) - 1;
                if(val < 0) val = 0;
#if 0
                char mvstr[16];
                mov_str(current->move,mvstr);
                print("%3d. %7s %9.2f %9.2f %9d %9d\n",
                    current->rank,mvstr,current->policy,
                    logistic(-current->score),current->visits,int(val));
#endif
            }

            if(diff > 0) 
                val += diff * current->policy;
            else
                val++;
            total_visits += val;
            probs[cnt] = val;
        }
        /*action value*/
        if(train_data_type == 1 || train_data_type == 2) {
            val = logistic(-current->score, 2 * Kfactor);
            if(train_data_type == 2)
                scores[cnt] = val;
            else
                probs[cnt] = val;
        }
        moves[cnt] = compute_move_index(current->move, 0);
        cnt++;
        current = current->next;
    }

    if(train_data_type != 1) {
        for(int i = 0; i < cnt; i++)
            probs[i] /= total_visits;
    }

    nmoves = cnt;

    /*best move*/
    MOVE move = stack[0].pv[0];
    bestm = compute_move_index(move, 0);
}

/*print training data*/
void print_train(int res, char* buffer, PTRAIN trn, PSEARCHER sb) {

    int bcount = 0;

    int pl = white;
    for(int h = 0; h < sb->hply; h++) {
        PTRAIN ptrn = &trn[h];
        PHIST_STACK phst = &sb->hstack[h];

        if(ptrn->nmoves >= 0) {
            strcpy(&buffer[bcount], ptrn->fen);
            bcount += strlen(ptrn->fen);

            if(res == R_WWIN) strcpy(&buffer[bcount]," 1-0");
            else if(res == R_BWIN) strcpy(&buffer[bcount]," 0-1");
            else {
                strcpy(&buffer[bcount]," 1/2-1/2");
                bcount += 4;
            }
            bcount += 4;

            bcount += sprintf(&buffer[bcount], " %f %d ", 
                ptrn->value, ptrn->nmoves);
            for(int i = 0; i < ptrn->nmoves; i++) {
                if(train_data_type == 2)
                    bcount += sprintf(&buffer[bcount], "%d %f %f ", 
                        ptrn->moves[i], ptrn->probs[i], ptrn->scores[i]);
                else
                    bcount += sprintf(&buffer[bcount], "%d %f ", 
                        ptrn->moves[i], ptrn->probs[i]);
            }

            bcount += sprintf(&buffer[bcount], "%d", ptrn->bestm);

            bcount += sprintf(&buffer[bcount], "\n");
        }

        pl = invert(pl);
    }

    l_lock(lock_io);
    fwrite(buffer, bcount, 1, spfile2);
    fflush(spfile2);
    l_unlock(lock_io);
}

/*job for selfplay thread*/
void SEARCHER::self_play_thread() {
    static VOLATILE int wins = 0, losses = 0, draws = 0;
    static const int vlimit = chess_clock.max_visits * frac_sv_low;
    MOVE move;
    int phply = hply;
    PTRAIN trn = new TRAIN[MAX_HSTACK];
    char* buffer = new char[4096 * MAX_HSTACK];

    unsigned start_t = get_time();
    static float average_npm = 0;
    float local_average_npm;

    while(true) {

        local_average_npm = 0;

        while(true) {

            /*game ended*/
            int res;
            if(hply >= 4 && -root_node->score >= sp_resign_value)
                res = (player == white) ? R_WWIN : R_BWIN;
            else
                res = print_result(false);

            /*write pgn and training data*/
            if(res != R_UNKNOWN) {
                if(res == R_DRAW) l_add(draws,1);
                else if(res == R_WWIN) l_add(wins,1);
                else if(res == R_BWIN) l_add(losses,1);
                int ngames = wins+losses+draws;
                print("[%d] Games %d: + %d - %d = %d\n",GETPID(),
                    ngames,wins,losses,draws);

                /*save pgn*/
                print_game(
                    res,spfile,"Training games",
                    "ScorpioZero","ScorpioZero",
                    ngames);

                /*save training data*/
                print_train(
                    res,buffer,trn,this);

                /*average npm*/
                l_lock(lock_io);
                average_npm +=
                    (local_average_npm - average_npm) / ngames;
                l_unlock(lock_io);

                /*abort*/
                if(ngames >= spgames)
                    abort_search = 1;
                else
                    break;
            }

            /*abort*/
            if(abort_search) {
                delete[] trn;
                delete[] buffer;

                if(processor_id == 0) {
                    float diff = (get_time() - start_t) / 60000.0;
                    int ngames = wins + losses + draws;
                    print("[%d] generated %d games in %.2f min : Rate %.2f games/min %.2f nodes/move\n",
                        GETPID(), ngames, diff, ngames / diff, average_npm);
                }

                return;
            }

            /*do fixed nodes mcts search*/
            generate_and_score_moves(0, -MATE_SCORE, MATE_SCORE);
            manage_tree(true);
            SEARCHER::egbb_ply_limit = 8;
            pstack->depth = search_depth * UNITDEPTH;
            stop_searcher = 0;

            /*katago's playout cap randomization*/
            unsigned int limit = 0;
            if(playout_cap_rand
                && chess_clock.max_visits >= 800 
                && (rand() > frac_full_playouts * RAND_MAX))
                limit = vlimit;

            search_mc(true,limit);
            move = stack[0].pv[0];

            local_average_npm += 
                (root_node->visits - local_average_npm) / (hply + 1);

#if 0
            char mvstr[16];
            mov_str(move,mvstr);
            print("%3d. %7s %d %5d = %8.2f\n",
                hply+1,mvstr,(limit > 0), root_node->visits, root_node->score);
#endif

            /*get training data*/
            PTRAIN ptrn = &trn[hply];
            if(pcr_write_low || (limit == 0)) { 
                get_train_data(
                    ptrn->value, ptrn->nmoves, ptrn->moves,
                    ptrn->probs, ptrn->scores, ptrn->bestm);
                get_fen(ptrn->fen);
            } else {
                ptrn->nmoves = -1;
            }

            /*we have a move, make it*/
            do_move(move);
        }

        int count = hply - phply;
        for(int i = 0; i < count; i++)
            undo_move();
    }
}

/*
Worker threads for PGN/EPD
*/
static ParallelFile* p_pgn = 0;
static int task = 0;
        
/*selfplay with multiple threads*/
void SEARCHER::worker_thread_all(ParallelFile* pgn, FILE* fw, int task_) {
    p_pgn = pgn;
    spfile = fw;
    if(task_ <= 3) {
        work_type = 1;
        task = task_;
    } else {
        work_type = 2;
        task = task_ - 4;
    }
    
    launch_worker_threads();
}

/*job for worker thread*/
void SEARCHER::worker_thread() {
    if(work_type == 0) {
        return self_play_thread();
    } else if(work_type == 1) {
        char game[32 * MAX_FILE_STR];
        while(p_pgn->next(game)) {
            pgn_to_epd(game,spfile,task);
        }
    } else {
        char epd[4 * MAX_FILE_STR];
        while(p_pgn->next(epd)) {
            epd_to_nn(epd,spfile,task);
        }
    }
}

/*select neural net and set parameters*/
void SEARCHER::select_net() {
    static int nn_type_o = nn_type;
    static int wdl_head_o = wdl_head;
    static double cpuct_init_o = cpuct_init;
    static double policy_temp_o = policy_temp;
    static double fpu_red_o = fpu_red;
    static int  fpu_is_loss_o = fpu_is_loss;

#define SET(p) {                      \
    nn_type = nn_type_##p;            \
    wdl_head = wdl_head_##p;          \
    cpuct_init = cpuct_init_##p;      \
    policy_temp = policy_temp_##p;    \
    fpu_red = fpu_red_##p;            \
    fpu_is_loss = fpu_is_loss_##p;    \
};

    ensemble = (ensemble_setting > 0) ? 1 : 0;

    if(all_man_c <= nn_man_e) {
        if(ensemble) return;

        if(nn_type_e >= 0 ) {
            nn_id = 2;
            SET(e);
        } else {
            nn_id = -nn_type_e - 1;
            if(nn_id == 0) { 
                SET(o);
            } else if(nn_id == 1) {
                SET(m);
            }
        }
    } else if(all_man_c <= nn_man_m) {
        if(ensemble) return;

        if(nn_type_m >= 0) {
            nn_id = 1;
            SET(m);
        } else {
            nn_id = -nn_type_m - 1;
            if(nn_id == 0) { 
                SET(o);
            } else if(nn_id == 2) {
                SET(e);
            }
        }
    } else {
        nn_id = 0;
        SET(o);
    }

#undef SET
}
/*
* Search parameters
*/
bool check_mcts_params(char** commands,char* command,int& command_num) {
    if(!strcmp(command, "cpuct_base")) {
        cpuct_base = atoi(commands[command_num++]);

    } else if(!strcmp(command, "cpuct_init")) {
        cpuct_init = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "policy_temp")) {
        policy_temp = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_red")) {
        fpu_red = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_is_loss")) {
        fpu_is_loss = atoi(commands[command_num++]);

    } else if(!strcmp(command, "cpuct_init_m")) {
        cpuct_init_m = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "policy_temp_m")) {
        policy_temp_m = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_red_m")) {
        fpu_red_m = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_is_loss_m")) {
        fpu_is_loss_m = atoi(commands[command_num++]);

    } else if(!strcmp(command, "cpuct_init_e")) {
        cpuct_init_e = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "policy_temp_e")) {
        policy_temp_e = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_red_e")) {
        fpu_red_e = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_is_loss_e")) {
        fpu_is_loss_e = atoi(commands[command_num++]);

    } else if(!strcmp(command, "noise_alpha")) {
        noise_alpha = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "noise_beta")) {
        noise_beta = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "noise_frac")) {
        noise_frac = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "temp_plies")) {
        temp_plies = atoi(commands[command_num++]);
    } else if(!strcmp(command, "rand_temp")) {
        rand_temp = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "rand_temp_end")) {
        rand_temp_end = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "playout_cap_rand")) {
        playout_cap_rand = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "frac_full_playouts")) {
        frac_full_playouts = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "frac_sv_low")) {
        frac_sv_low = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "pcr_write_low")) {
        pcr_write_low = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "kld_threshold")) {
        kld_threshold = atoi(commands[command_num++]) / 1000000.0;
    } else if(!strcmp(command, "early_stop")) {
        early_stop = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "train_data_type")) {
        train_data_type = atoi(commands[command_num++]);
    } else if(!strcmp(command, "reuse_tree")) {
        reuse_tree = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "ensemble")) {
        ensemble_setting = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "ensemble_type")) {
        ensemble_type = atoi(commands[command_num++]);
    } else if(!strcmp(command, "min_policy_value")) {
        min_policy_value = atoi(commands[command_num++]) / 1000.0;
    } else if(!strcmp(command, "backup_type")) {
        backup_type_setting = atoi(commands[command_num++]);
    } else if(!strcmp(command, "frac_alphabeta")) {
        frac_alphabeta = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "frac_freeze_tree")) {
        frac_freeze_tree = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "frac_abrollouts")) {
        frac_abrollouts = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "frac_abprior")) {
        frac_abprior = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "mcts_strategy_depth")) {
        mcts_strategy_depth = atoi(commands[command_num++]);
    } else if(!strcmp(command, "alphabeta_depth")) {
        alphabeta_depth = atoi(commands[command_num++]);
    } else if(!strcmp(command, "evaluate_depth")) {
        evaluate_depth = atoi(commands[command_num++]);
    } else if(!strcmp(command, "virtual_loss")) {
        virtual_loss = atoi(commands[command_num++]);
    } else if(!strcmp(command, "visit_threshold")) {
        visit_threshold = atoi(commands[command_num++]);
    } else if(!strcmp(command, "montecarlo")) {
        montecarlo = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "sp_resign_value")) {
        sp_resign_value = atoi(commands[command_num++]);
    } else if(!strcmp(command, "forced_playouts")) {
        forced_playouts = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "policy_pruning")) {
        policy_pruning = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "treeht")) {
        UBMP32 ht = atoi(commands[command_num++]);
        UBMP32 size = ht * (double(1024 * 1024) / node_size);
        double size_mb = (size / double(1024 * 1024)) * node_size;
        print("treeht %d X %d = %.1f MB\n",size, node_size,size_mb);
        Node::max_tree_nodes = unsigned((size_mb / node_size) * 1024 * 1024);
    } else {
        return false;
    }
    return true;
}
void print_mcts_params() {
    print_spin("cpuct_base",cpuct_base,0,100000000);

    print_spin("cpuct_init",int(cpuct_init*100),0,1000);
    print_spin("policy_temp",int(policy_temp*100),0,1000);
    print_spin("fpu_red",int(fpu_red*100),-1000,1000);
    print_spin("fpu_is_loss",fpu_is_loss,-1,1);

    print_spin("cpuct_init_m",int(cpuct_init_m*100),0,1000);
    print_spin("policy_temp_m",int(policy_temp_m*100),0,1000);
    print_spin("fpu_red_m",int(fpu_red_m*100),-1000,1000);
    print_spin("fpu_is_loss_m",fpu_is_loss_m,-1,1);

    print_spin("cpuct_init_e",int(cpuct_init_e*100),0,1000);
    print_spin("policy_temp_e",int(policy_temp_e*100),0,1000);
    print_spin("fpu_red_e",int(fpu_red_e*100),-1000,1000);
    print_spin("fpu_is_loss_e",fpu_is_loss_e,-1,1);

    print_spin("noise_alpha",int(noise_alpha*100),0,100);
    print_spin("noise_beta",int(noise_beta*100),0,100);
    print_spin("noise_frac",int(noise_frac*100),0,100);
    print_spin("temp_plies",temp_plies,0,100);
    print_spin("rand_temp",int(rand_temp*100),0,1000);
    print_spin("rand_temp_end",int(rand_temp_end*100),0,1000);
    print_check("playout_cap_rand",playout_cap_rand);
    print_spin("frac_full_playouts",int(frac_full_playouts*100),0,100);
    print_spin("frac_sv_low",int(frac_sv_low*100),0,100);
    print_check("pcr_write_low",pcr_write_low);
    print_spin("kld_threshold",int(kld_threshold*1000000),0,1000000);
    print_check("early_stop",early_stop);
    print_spin("train_data_type",train_data_type,0,2);
    print_check("reuse_tree",reuse_tree);
    print_spin("backup_type",backup_type_setting,0,7);
    print_spin("ensemble",int(ensemble_setting*100),0,100);
    print_spin("ensemble_type",ensemble_type,0,1);
    print_spin("min_policy_value",int(min_policy_value*1000),0,1000);
    print_spin("frac_alphabeta",int(frac_alphabeta*100),0,100);
    print_spin("frac_freeze_tree",int(frac_freeze_tree*100),0,100);
    print_spin("frac_abrollouts",int(frac_abrollouts*100),0,100);
    print_spin("frac_abprior",int(frac_abprior*100),0,100);
    print_spin("mcts_strategy_depth",mcts_strategy_depth,0,100);
    print_spin("alphabeta_depth",alphabeta_depth,1,100);
    print_spin("evaluate_depth",evaluate_depth,-4,100);
    print_spin("virtual_loss",virtual_loss,0,1000);
    print_spin("visit_threshold",visit_threshold,0,1000000);
    print_check("montecarlo",montecarlo);
    print_spin("sp_resign_value",sp_resign_value,0,10000);
    print_check("forced_playouts",forced_playouts);
    print_check("policy_pruning",policy_pruning);
    print_spin("treeht",int((Node::max_tree_nodes / double(1024*1024)) * node_size),0,131072);
}
