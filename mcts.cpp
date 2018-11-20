#include "scorpio.h"

/*mcts parameters*/
static double  UCTKmax = 0.3;
static double  UCTKmin = 0.005;
static double  dUCTK = UCTKmax;
static int  reuse_tree = 1;
static int  backup_type = MINMAX;
static double frac_alphabeta = 1.0; 
static double frac_freeze_tree = 0.3;
static double frac_abrollouts = 0.2;
double frac_abprior = 0.3;
static int  mcts_strategy_depth = 30;
static int  alphabeta_depth = 1;
static int  evaluate_depth = 0;
static double  frac_width = 1.0;
static int virtual_loss = 3;
static unsigned int visit_threshold = 800;

int montecarlo = 0;
int rollout_type = ALPHABETA;
bool freeze_tree = false;

/*Node*/
std::vector<Node*> Node::mem_[MAX_CPUS];
VOLATILE unsigned int Node::total_nodes = 0;
unsigned int Node::max_tree_nodes = 0;
unsigned int Node::max_tree_depth = 0;
Node* VOLATILE SEARCHER::root_node = 0;
HASHKEY SEARCHER::root_key = 0;

static const int MEM_INC = 1024;

Node* Node::allocate(int id) {
    Node* n;
    
    if(mem_[id].empty()) {
        n = new Node[MEM_INC];
        for(int i = 0;i < MEM_INC;i++)
            mem_[id].push_back(&n[i]);
    }
    n = mem_[id].back();
    mem_[id].pop_back();
    l_add(total_nodes,1);

    n->clear();
    return n;
}

Node* Node::reclaim(Node* n,int id,MOVE* except) {
    if(id < 0) {
        unsigned mini = 0;
        unsigned mins = mem_[mini].size();
        for(int i = 1;i < PROCESSOR::n_processors;i++) {
            if(mem_[i].size() < mins) {
                mins = mem_[i].size();
                mini = i;
            }
        }
        id = mini;
    }
    Node* current = n->child,*rn = 0;
    while(current) {
        if(except && (current->move == *except)) 
            rn = current;
        else if(!current->child) {
            mem_[id].push_back(current);
            total_nodes--;
        } else 
            reclaim(current,id);
        current = current->next;
    }
    mem_[id].push_back(n);
    total_nodes--;
    return rn;
}

void Node::rank_children(Node* n) {

    /*rank all children*/
    Node* current = n->child, *best = 0;
    int brank = MAX_MOVES - 1;

    while(current) {
        /*rank subtree first*/
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

void Node::reset_bounds(Node* n,int alpha,int beta) {
    Node* current = n->child;
    while(current) {
        reset_bounds(current,-beta,-alpha);
        current->flag = 0;
        current->busy = 0;
        current = current->next;
    }
    n->alpha = alpha;
    n->beta = beta;
}

static const double Kfactor = -log(10.0) / 400.0;

double logistic(double score) {
    return 1 / (1 + exp(Kfactor * score));
}

double logit(double p) {
    if(p < 1e-15) p = 1e-15;
    else if(p > 1 - 1e-15) p = 1 - 1e-15;
    return log((1 - p) / p) / Kfactor;
}

Node* Node::Max_UCB_select(Node* n) {
    double logn = log(double(n->visits));
    double uct, bvalue = -2;
    Node* current, *bnode = 0;
    unsigned vst;
    bool has_ab = (n == SEARCHER::root_node && frac_abprior > 0);

    current = n->child;
    while(current) {
        if(current->move) {

            vst = current->visits;
#ifdef PARALLEL
            vst += virtual_loss * current->get_busy();
#endif          

            uct = logistic(-current->score);
            if(has_ab)
                uct += logistic(-current->heuristic);
            else
                uct += logistic(-current->heuristic) / vst;
            uct += dUCTK * sqrt(logn / vst);

            if(uct > bvalue) {
                bvalue = uct;
                bnode = current;
            }

        }
        current = current->next;
    }

    return bnode;
}
Node* Node::Max_AB_select(Node* n,int alpha,int beta,bool try_null,bool search_by_rank) {
    double bvalue = -MAX_NUMBER, uct;
    Node* current, *bnode = 0;
    int alphac, betac;

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

    return bnode;
}
/*
Utility function for random selection of action from 
discrete probability distribution
*/
static int findCeil(int arr[], int r, int l, int h) {
    int mid;
    while (l < h) {
         mid = l + ((h - l) >> 1);
        (r > arr[mid]) ? (l = mid + 1) : (h = mid);
    }
    return (arr[l] >= r) ? l : -1;
}
static Node* pickRandMove(Node* arr[], int freq[], int n) {
    int prefix[n];
    prefix[0] = freq[0];
    for(int i = 1; i < n; ++i)
        prefix[i] = prefix[i - 1] + freq[i];

    int r = (rand() % prefix[n - 1]) + 1;
    int indexc = findCeil(prefix, r, 0, n - 1);

    return arr[indexc];
}
 
static Node* node_pt[MAX_MOVES];
static int frequency[MAX_MOVES];

Node* Node::Random_select(Node* n) {
    Node* current, *bnode = n->child;
    int count, total_visits;

    count = 0;
    current = n->child;
    total_visits = 0;
    while(current) {
        if(current->move && current->visits > 1) {
            count++;
            total_visits += current->visits;
        }
        current = current->next;
    }

    if(count) {
        count = 0;
        current = n->child;
        while(current) {
            if(current->move && current->visits > 1) {
                node_pt[count] = current;
                frequency[count] = current->visits;
                count++;
            }
            current = current->next;
        }
        bnode = pickRandMove(node_pt,frequency,count);
    }

    return bnode;
}

Node* Node::Best_select(Node* n) {
    double bvalue = -MAX_NUMBER;
    Node* current = n->child, *bnode = n->child;
    bool has_ab = (n == SEARCHER::root_node && frac_abprior > 0);

    while(current) {
        if(current->move && current->visits > 1) {
            if(rollout_type == MCTS ||
                (rollout_type == ALPHABETA && 
                /* must be finished or ranked first */
                (current->alpha >= current->beta ||
                 current->rank == 1)) 
            ) {
                double val = -current->score;
                if(has_ab)
                    val += -current->heuristic;
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
    Node* current = n->child, *bnode = 0;
    float bscore = MAX_NUMBER;
    while(current) {
        if(current->move && current->visits > 1) {
            if(current->score < bscore) {
                bscore = current->score;
                bnode = current;
            }
        }
        current = current->next;
    }
    if(!bnode) bnode = n->child;
    return bnode->score;
}
float Node::Avg_score(Node* n) {
    double tvalue = 0;
    unsigned int tvisits = 0;
    Node* current = n->child;
    while(current) {
        if(current->move && current->visits > 1) {
            tvalue += logistic(current->score) * current->visits;
            tvisits += current->visits;
        }
        current = current->next;
    }
    if(tvisits == 0) {
        tvalue = logistic(n->child->score);
        tvisits = 1;
    }
    return logit(tvalue / tvisits);
}
float Node::Avg_score_mem(Node* n, double score, int visits) {
    if(n->visits == 1) return score;
    float sc = logistic(n->score);
    float sc1 = logistic(score);
    sc += (sc1 - sc) * visits / (n->visits + visits);
    return logit(sc);
}
void Node::Backup(Node* n,double& score,int visits, int all_man_c) {
    /*Compute parent's score from children*/
    if(all_man_c <= 10)
        score = -Min_score(n);
    else if(backup_type == MIX_VISIT) {
        if(n->visits > visit_threshold)
            score = -Min_score(n);
        else
            score = Avg_score_mem(n,score,visits);
    } 
    else if(backup_type == CLASSIC)
        score = Avg_score_mem(n,score,visits);
    else if(backup_type == AVERAGE)
        score = -Avg_score(n);
    else {
        if(backup_type == MINMAX || backup_type == MINMAX_MEM)
            score = -Min_score(n);
        else if(backup_type == MIX  || backup_type == MIX_MEM)
            score = -(3 * Min_score(n) + Avg_score(n)) / 4;

        if(backup_type >= MINMAX_MEM)
            score = Avg_score_mem(n,score,visits);
    }

    /*Update alpha-beta bounds. Note: alpha is updated only from 
      child just searched (next), beta is updated from remaining 
      unsearched children */
    if(rollout_type == MCTS) {
        n->update_score(score);
    } else if(n->alpha < n->beta) {
        n->update_score(score);
        int alpha = -MATE_SCORE;
        int beta = -MATE_SCORE;
        Node* current = n->child;
        while(current) {
            if(current->move) {
                if(-current->beta > alpha) alpha = -current->beta;
                if(-current->alpha > beta) beta = -current->alpha;
            }
            current = current->next;
        }
        if(n->alpha >= alpha)
            alpha = n->alpha;
        if(n->beta <= beta)
            beta = n->beta;
        n->set_bounds(alpha,beta);
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

    bool save = skip_nn;
    skip_nn = true;

    /*generate and score moves*/
    if(ply)
        generate_and_score_moves(evaluate_depth,-MATE_SCORE,MATE_SCORE);

    /*add nodes to tree*/
    add_children(n);

    /*add null move*/
    if(use_nullmove
        && n->child
        && ply > 0
        && !hstack[hply - 1].checks
        && piece_c[player]
        && hstack[hply - 1].move != 0
        ) {
        add_null_child(n);
    }

    skip_nn = save;
}
void SEARCHER::add_children(Node* n) {
    Node* last = n;
    for(int i = 0;i < pstack->count; i++) {
        Node* node = Node::allocate(processor_id);
        node->move = pstack->move_st[i];
        node->score = -pstack->score_st[i];
        node->visits = 1;
        node->alpha = -MATE_SCORE;
        node->beta = MATE_SCORE;
        node->heuristic = node->score;
        node->rank = i + 1;
        if(last == n) last->child = node;
        else last->next = node;
        last = node;
    }
}
void SEARCHER::add_null_child(Node* n) {
    Node* current = n->child, *last = 0;
    while(current) {
        last = current;
        current = current->next;
    }
    Node* node = Node::allocate(processor_id);
    node->move = 0;
    PUSH_NULL();
    node->score = eval();
    POP_NULL();
    node->visits = 1;
    node->alpha = -MATE_SCORE;
    node->beta = MATE_SCORE;
    node->heuristic = node->score;
    node->rank = 0;
    last->next = node;
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
    if(!n->child) {

        /*run out of memory for nodes*/
        if(rollout_type == ALPHABETA && 
             (
             Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes       
             || freeze_tree
             || pstack->depth <= alphabeta_depth * UNITDEPTH
             )
            ) {
            if(Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes &&
                !freeze_tree && processor_id == 0) {
                freeze_tree = true;
                print("Maximum number of nodes used.\n");
            }
            search_calls++;
            score = get_search_score();
            if(stop_searcher || abort_search)
                goto FINISH;
        /*create children*/
        } else {

            if(n->try_create()) {
                create_children(n);
                n->clear_create();
            } else {
                visits = 0;
                score = n->score;
                goto FINISH;
            }

            if(!n->child) {
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
                    Node* current = n->child;
                    if(use_nn) {
                        PUSH_MOVE(current->move);
                        current->score = eval() + (current->score - eval(true));
                        POP_MOVE();
                    }
                    score = -current->score;
                    current->visits++;
                    
                    Node::Backup(n,score,visits,all_man_c);
                    goto FINISH;
                }
            }
        }

BACKUP_LEAF:
        Node::BackupLeaf(n,score);
        /*Hack: fake a hard NN eval (without cache) request to 
          make multi-threaded batching work. We need something better! */
        if(use_nn)
            probe_neural();

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
                try_null,search_by_rank);
        } else {
            next = Node::Max_UCB_select(n);
        }

        /*This could happen in parallel search*/
        if(!next) goto FINISH;

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
                              abs(betac) != MATE_SCORE &&
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
                    && abs(betac) != MATE_SCORE 
                    ) {
                    visits = 1;
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
            if(rollout_type == ALPHABETA
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
                        Node::reset_bounds(next,alphac,betac);
                        goto RESEARCH;
                    } else {
                        next->set_bounds(alphac,betac);
                    }
                }
            }

            /*Undo move*/
            POP_MOVE();
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
            if(next->alpha >= next->beta) {
                if(score >= pstack->beta)
                    n->set_bounds(score,score);
            }
            goto FINISH;
            /*end null move*/
        }
BACKUP:
        /*Backup score and bounds*/
        Node::Backup(n,score,visits,all_man_c);

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

void SEARCHER::search_mc() {
    Node* root = root_node;
    double pfrac = 0,score;
    int visits;
    int oalpha = pstack->alpha;
    int obeta = pstack->beta;
    unsigned int ovisits = root->visits;
#ifdef EGBB
    unsigned int visits_poll;
    if(use_nn) visits_poll = PROCESSOR::n_processors;
    else visits_poll = 200;
#else
    const unsigned int visits_poll = 200;
#endif

    /*wait until all idle processors are awake*/
    static VOLATILE int t_count = 0;
    int p_t_count = l_add(t_count,1);
    if(p_t_count == PROCESSOR::n_processors - 1)
        t_count = 0;
    while(t_count > 0 && t_count < PROCESSOR::n_processors) {
        t_yield();
        if(SEARCHER::use_nn) t_sleep(SEARCHER::delay);
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
        if(root->visits >= (unsigned)chess_clock.max_visits) {
            abort_search = 1;
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

        /*book keeping*/
        if(processor_id == 0) {

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

                    dUCTK = UCTKmax - frac * (UCTKmax - UCTKmin);
                    if(dUCTK < UCTKmin) dUCTK = UCTKmin;
                    if(frac - pfrac >= 0.1) {
                        pfrac = frac;
                        if(rollout_type == MCTS) {
                            extract_pv(root);
                            root_score = root->score;
                            print_pv(root->score);
                            search_depth++;
                        }
                    }
                    /*stop growing tree after some time*/
                    if(rollout_type == ALPHABETA && !freeze_tree && frac_freeze_tree < 1.0 &&
                        frac >= frac_freeze_tree * frac_alphabeta) {
                        freeze_tree = true;
                        print("Freezing tree.\n");
                    }
                    /*Switching rollouts type*/
                    if(rollout_type == ALPHABETA && frac_alphabeta != 1.0 
                        && frac > frac_alphabeta) {
                        print("Switching rollout type to MCTS.\n");
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
    if(master) {
        l_lock(lock_smp);
        l_lock(master->lock);
        update_master(1);
        l_unlock(master->lock);
        l_unlock(lock_smp);
    } else if(!abort_search && !stop_searcher) {
        bool failed = (-best->score <= oalpha) || 
                      (-best->score >= obeta);
        if(!failed)
            best = Node::Best_select(root);

        root->score = -best->score;
        root_score = root->score;
        pstack->best_score = root_score;

        best->score = -MATE_SCORE;
        extract_pv(root);
        best->score = -root_score;

        if(!failed)     
            print_pv(root_score);
    } else if(is_selfplay && hply <= 30) {
        /*Random selection for self play*/
        for (int j = ply; j > 0 ; j--) {
            MOVE move = hstack[hply - 1].move;
            if(move) POP_MOVE();
            else POP_NULL();
        }
        extract_pv(root,true);
    }
}
/*
Manage search tree
*/
void SEARCHER::manage_tree(Node*& root, HASHKEY& root_key) {
    /*find root node*/
    if(root) {
        int i,j;
        bool found = false;
        for(i = 0;i < 8;i++) {
            if(hstack[hply - 1 - i].hash_key == root_key) {
                found = true;
                break;
            }
        }
        if(found && reuse_tree) {
            MOVE move;
            for(j = i;j >= 0;--j) {
                move = hstack[hply - 1 - j].move;
                root = Node::reclaim(root,-1,&move);
                if(!root) break;
            }
        } else {
            Node::reclaim(root);
            root = 0;
        }
    }
    if(!root) {
        print_log("[Tree-not-found]\n");
        root = Node::allocate(processor_id);
    } else {
        print_log("[Tree-found : visits %d score %d]\n",
            root->visits,int(root->score));

        /*remove null moves from root*/
        Node* current = root->child, *prev;
        while(current) {
            prev = current;
            current = current->next;
            if(current && current->move == 0) {
                prev->next = current->next;
                Node::reclaim(current);
            }
        }
    }
    if(!root->child) {
        create_children(root);
        root->visits++;
    }
    root->set_pvmove();
    root_key = hash_key;

    /*only have root child*/
    if(!freeze_tree && frac_freeze_tree == 0)
        freeze_tree = true;
    if(frac_alphabeta == 0) {
        rollout_type = MCTS;
        search_depth = MAX_PLY - 2;
    } else {
        rollout_type = ALPHABETA;
        use_nn = 0;
    }

#ifndef NODES_PRIOR
    /*alpha-beta prior*/
    if(frac_abprior > 0) {

        /*do ab search*/
#ifdef PARALLEL
        for(int i = PROCESSOR::n_cores;i < PROCESSOR::n_processors;i++)
            processors[i]->state = PARK;
#endif
        montecarlo = 0;
        use_nn = 0;

        chess_clock.set_stime(hply,false);
        chess_clock.search_time *= frac_abprior;
        print("Searching root moves with alpha-beta\n");
        int d;
        for(d = 2; d < MAX_PLY - 1 ; d++) {
            evaluate_moves(d,-MATE_SCORE,MATE_SCORE);
            if(stop_searcher || abort_search)
                break;
        }
        print("Finished with depth %d in %dms\n",
            d, get_time() - start_time);
        stop_searcher = 0;
        abort_search = 0;

        use_nn = save_use_nn;
        montecarlo = 1;
#ifdef PARALLEL
        for(int i = PROCESSOR::n_cores;i < PROCESSOR::n_processors;i++)
            processors[i]->state = WAIT;
#endif
        
        /*assign prior*/
        Node* current = root->child;
        while(current) {
            for(int i = 0;i < pstack->count; i++) {
                MOVE& move = pstack->move_st[i];
                if(move == current->move) {
                    current->heuristic = -pstack->score_st[i];
                    current->rank = i + 1;
                    break;
                }
            }
            current = current->next;
        }
    }
#endif
}
/*
* Search parameters
*/
bool check_mcts_params(char** commands,char* command,int& command_num) {
    if(!strcmp(command, "UCTKmin")) {
        UCTKmin = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "UCTKmax")) {
        UCTKmax = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "reuse_tree")) {
        reuse_tree = atoi(commands[command_num++]);
    } else if(!strcmp(command, "backup_type")) {
        backup_type = atoi(commands[command_num++]);
    } else if(!strcmp(command, "frac_alphabeta")) {
        frac_alphabeta = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "frac_freeze_tree")) {
        frac_freeze_tree = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "frac_abrollouts")) {
        frac_abrollouts = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "frac_abprior")) {
        frac_abprior = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "frac_width")) {
        frac_width = atoi(commands[command_num++]) / 100.0;
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
        montecarlo = atoi(commands[command_num++]);
    } else if(!strcmp(command, "treeht")) {
        UBMP32 ht = atoi(commands[command_num++]);
        UBMP32 size = ht * ((1024 * 1024) / sizeof(Node));
        print("treeht %d X %d = %.1f MB\n",size,sizeof(Node),
            (size * sizeof(Node)) / double(1024 * 1024));
        Node::max_tree_nodes = size / PROCESSOR::n_processors;
    } else {
        return false;
    }
    return true;
}
void print_mcts_params() {
    print("feature option=\"UCTKmin -spin %d 0 100\"\n",int(UCTKmin*100));
    print("feature option=\"UCTKmax -spin %d 0 100\"\n",int(UCTKmax*100));
    print("feature option=\"reuse_tree -check %d\"\n",reuse_tree);
    print("feature option=\"backup_type -combo *MINMAX AVERAGE MIX MINMAX_MEM AVERAGE_MEM MIX_MEM CLASSIC MIX_VISIT\"\n");
    print("feature option=\"frac_alphabeta -spin %d 0 100\"\n",int(frac_alphabeta*100));
    print("feature option=\"frac_freeze_tree -spin %d 0 100\"\n",int(frac_freeze_tree*100));
    print("feature option=\"frac_abrollouts -spin %d 0 100\"\n",int(frac_abrollouts*100));
    print("feature option=\"frac_abprior -spin %d 0 100\"\n",int(frac_abprior*100));
    print("feature option=\"frac_width -spin %d 0 1000\"\n",int(frac_width*100));
    print("feature option=\"mcts_strategy_depth -spin %d 0 100\"\n",mcts_strategy_depth);
    print("feature option=\"alphabeta_depth -spin %d 1 100\"\n",alphabeta_depth);
    print("feature option=\"evaluate_depth -spin %d -4 100\"\n",evaluate_depth);
    print("feature option=\"virtual_loss -spin %d 0 1000\"\n",virtual_loss);
    print("feature option=\"visit_threshold -spin %d 0 1000\"\n",visit_threshold);
    print("feature option=\"montecarlo -check %d\"\n",montecarlo);
    print("feature option=\"treeht -spin %d 0 131072\"\n",
        int((Node::max_tree_nodes * sizeof(Node)) / double(1024*1024)));
}
