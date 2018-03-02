#include "scorpio.h"

/*mcts parameters*/
static double  UCTKmax = 0.3;
static double  UCTKmin = 0.3;
static double  dUCTK = UCTKmax;
static int  reuse_tree = 1;
static int  backup_type = MINMAX;
static double frac_alphabeta = 1.0; 
static double frac_freeze_tree = 0.3;
static int  mcts_strategy_depth = 15;

int montecarlo = 0;
int rollout_type = ALPHABETA;
bool freeze_tree = false;

/*Node*/
LOCK Node::mem_lock = 0;
std::list<Node*> Node::mem_;
unsigned int Node::total_nodes = 0;
unsigned int Node::max_tree_nodes = 0;
int Node::maxuct = 0;
int Node::maxply = 0;
Node* VOLATILE SEARCHER::root_node = 0;
HASHKEY SEARCHER::root_key = 0;

static const int MEM_INC = 1024;

Node* Node::allocate() {
    Node* n;
    
    l_lock(mem_lock);
    if(mem_.empty()) {
        n = new Node[MEM_INC];
        for(int i = 0;i < MEM_INC;i++)
            mem_.push_back(&n[i]);
    }
    n = mem_.front();
    mem_.pop_front();
    total_nodes++;
    l_unlock(mem_lock);

    n->clear();
    return n;
}
void Node::release(Node* n) {
    l_lock(mem_lock);
    mem_.push_front(n);
    total_nodes--;
    l_unlock(mem_lock);
}
Node* Node::reclaim(Node* n,MOVE* except) {
    Node* current = n->child,*rn = 0;
    while(current) {
        if(except && (current->move == *except)) rn = current;
        else reclaim(current);
        current = current->next;
    }
    Node::release(n);
    return rn;
}

void Node::rank_children(Node* n) {

    /*rank all children*/
    Node* current = n->child, *best = 0;
    int brank = MAX_MOVES;

    while(current) {
        /*rank subtree first*/
        rank_children(current);

        /*rank current node*/
        if(current->move) {
            double val = current->visits;
            if(current->is_pvmove())
                val += MAX_HIST;

            /*find rank of current child*/
            int rank = 1;
            Node* cur = n->child;
            while(cur) {
                if(cur->move && cur != current) {
                    double val1 = cur->visits;
                    if(cur->is_pvmove())
                        val1 += MAX_HIST;

                    if(val1 > val) rank++;
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
    if(best) {
        best->rank = 1;
    }
}

void Node::reset_bounds(Node* n,int alpha,int beta) {
    Node* current = n->child;
    while(current) {
        reset_bounds(current,-beta,-alpha);
        current = current->next;
    }
    n->alpha = alpha;
    n->beta = beta;
    n->set_active();
}

static inline float logistic(float eloDelta) {
    static const double K = -log(10.0) / 400.0;
    return 1 / (1 + exp(K * eloDelta));
}

Node* Node::Max_UCB_select(Node* n) {
    double logn = log(double(n->visits));
    double uct, bvalue = -1;
    Node* current, *bnode = 0;

    current = n->child;
    while(current) {
        if(!current->move) {
            current->clear_active();
        } else {
            uct = logistic(-current->score) +
                  dUCTK * sqrt(logn / current->visits);
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

        if(!use_ab || alphac < betac) {
            /*base score*/
            if(use_ab) uct = -current->score;
            else uct = -current->visits;
            /*nullmove score*/
            if(!current->move) {
                if(try_null) {
                    uct = MATE_SCORE - 1;
                    current->set_active();
                } else {
                    uct = -MATE_SCORE;
                    current->clear_active();
                }
            }
            /*pv node*/
            else if(search_by_rank)
                uct = MAX_MOVES - current->rank;
#ifdef PARALLEL
            /*ABDADA like move selection*/
            if(current->is_busy()) uct -= 1000;
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
Node* Node::Max_visits_select(Node* n) {
    unsigned int max_visits = 0;
    Node* current = n->child, *best = n->child;
    while(current) {
        if(current->is_active()) {
            if(current->visits > max_visits) {
                max_visits = current->visits;
                best = current;
            }
        }
        current = current->next;
    }
    return best;
}
Node* Node::Max_score_select(Node* n) {
    double bvalue = -MAX_NUMBER;
    Node* current = n->child, *bnode = n->child;

    while(current) {
        if(current->is_active()) {
            double val = -current->score;
            if(val > bvalue || (val == bvalue 
                && current->rank < bnode->rank)) {
                bvalue = val;
                bnode = current;
            }
        }
        current = current->next;
    }

    return bnode;
}
Node* Node::Max_pv_select(Node* n) {
    double bvalue = -MAX_NUMBER;
    Node* current = n->child, *bnode = n->child;

    while(current) {
        if(current->is_active()) {
            if(rollout_type == MCTS ||
                (rollout_type == ALPHABETA && 
                /* must be finished or ranked first */
                (current->alpha >= current->beta ||
                 current->rank == 1)) 
            ) {
                double val = -current->score;
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
Node* Node::Best_select(Node* n) {
    if(backup_type == AVERAGE)
        return Max_visits_select(n);
    else
        return Max_pv_select(n);  
}
void SEARCHER::create_children(Node* n) {
    /*lock*/
    l_lock(n->lock);
    if(n->child 
        || Node::total_nodes + MAX_MOVES >= Node::max_tree_nodes 
        ) {
        l_unlock(n->lock);
        return;
    }

    /*maximum tree depth*/
    if(ply > Node::maxuct)
        Node::maxuct = ply;

    /*generate and score moves*/
    if(ply)
        generate_and_score_moves(0,-MATE_SCORE,MATE_SCORE);

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

    /*unlock*/
    l_unlock(n->lock);
}
void SEARCHER::add_children(Node* n) {
    Node* last = n;
    for(int i = 0;i < pstack->count; i++) {
        Node* node = Node::allocate();
        node->move = pstack->move_st[i];
        node->score = -pstack->score_st[i];
        node->visits = 1;
        node->alpha = -MATE_SCORE;
        node->beta = MATE_SCORE;
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
    Node* node = Node::allocate();
    node->move = 0;
    PUSH_NULL();
    node->score = eval();
    POP_NULL();
    node->visits = 1;
    node->alpha = -MATE_SCORE;
    node->beta = MATE_SCORE;
    node->rank = 0;
    last->next = node;
}
void SEARCHER::play_simulation(Node* n, double& score, int& visits) {

    nodes++;

    /*In parallel search, current node's window may be closed
      by another thread, in which case we return immediately*/
#ifdef PARALLEL
    if(rollout_type == ALPHABETA) {
        l_lock(n->lock);
        if(n->alpha >= n->beta) {
            visits = 1;
            score = n->score;
            l_unlock(n->lock);
            return;
        }
        l_unlock(n->lock);
    }
#endif

    /*virtual loss*/
    l_lock(n->lock);
    n->visits++;
    n->set_busy();
    l_unlock(n->lock);

    /*Draw*/
    if(draw()) {
        score = ((scorpio == player) ? -contempt : contempt);
        goto LEAF;
    /*bitbases*/
    } else if(bitbase_cutoff()) {
        score = pstack->best_score;
        goto LEAF;
    /*Reached max plies and depth*/
    } else if(ply >= MAX_PLY - 1 || pstack->depth <= 0) {
        score = n->score;
        goto LEAF;
    /*mate distance pruning*/
    } else if(n->alpha > MATE_SCORE - WIN_PLY * (ply + 1)) {
        score = n->alpha;
        goto LEAF;
    /*No children*/
    } else if(!n->child) {

        /*run out of memory for nodes*/
        if(rollout_type == ALPHABETA && 
            (Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes       
             || freeze_tree)
            ) {
            if(!freeze_tree && processor_id == 0) {
                freeze_tree = true;
                print("Maximum number of nodes used.\n");
            }
            search_calls++;
            score = get_search_score();
            if(stop_searcher || abort_search)
                goto FINISH;
        /*create children*/
        } else {
            create_children(n);
            if(!n->child) {
                if(hstack[hply - 1].checks)
                    score = -MATE_SCORE + WIN_PLY * (ply + 1);
                else 
                    score = 0;
            } else {
                if(rollout_type == ALPHABETA
                    && pstack->depth > UNITDEPTH) {
                    goto SELECT;
                } else  {
                    Node::maxply += (ply + 1);
                    score = -n->child->score;
                    visits = pstack->count;
                    nodes += visits;
                    goto UPDATE;
                }
            }
        }
LEAF:
        /*visits and maxply*/
        visits = 1;
        Node::maxply += ply;

        /*update alpha-beta bounds*/
        if(rollout_type == ALPHABETA) {
            l_lock(n->lock);
            n->alpha = score;
            n->beta = score;
            l_unlock(n->lock);
        }

    /*Has children*/
    } else {

SELECT:
        /*select move*/
        Node* next;
        if(rollout_type == ALPHABETA) {
            bool try_null = pstack->node_type != PV_NODE
                            && pstack->depth >= 4 * UNITDEPTH 
                            && n->score >= pstack->beta;
            bool search_by_rank = (n == root_node);

            next = Node::Max_AB_select(n,-pstack->beta,-pstack->alpha,
                try_null,search_by_rank);
        } else {
            next = Node::Max_UCB_select(n);
        }

        /*This could happen in parallel search*/
        if(!next) next = n->child;

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
                    Node::maxply += ply;
                    l_lock(next->lock);
                    next->alpha = betac;
                    next->beta = betac;
                    l_unlock(next->lock);
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
                    try_scout = false;
                    next_node_t = PV_NODE;
                    alphac = -(pstack - 1)->beta;
                    betac = -(pstack - 1)->alpha;
                    Node::reset_bounds(next,alphac,betac);
                    next->set_failed_scout();
                    goto RESEARCH;
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
                if(score >= pstack->beta) {
                    l_lock(n->lock);
                    n->alpha = score;
                    n->beta = score;
                    l_unlock(n->lock);
                } else {
                    next->clear_active();
                }
            }
            score = n->score;
            goto UPDATE;
            /*end null move*/
        }
        
BACKUP:
        /*Do minmax style backup here, AVERAGE backup doesn't need
          additional work.*/
        if(backup_type == MINMAX) {

            /*Update score. Note that currently unsearched children 
              use their scores from a previous ID search */
            Node* best = Node::Max_score_select(n);
            score = -best->score;

            /*Update alpha-beta bounds. Note:
              alpha is updated only from child just searched (next)
              beta is updated from remaining unsearched children */
            if(rollout_type == ALPHABETA) {
                int alpha = -MATE_SCORE;
                int beta = -MATE_SCORE;
                Node* current = n->child;
                while(current) {
                    if(current->is_active()) {
                        if(-current->beta > alpha) alpha = -current->beta;
                        if(-current->alpha > beta) beta = -current->alpha;
                    }
                    current = current->next;
                }
                l_lock(n->lock);
                if(n->alpha < alpha)
                    n->alpha = alpha;
                if(n->beta > beta)
                    n->beta = beta;
                l_unlock(n->lock);

                /*Update bounds at root*/
                if(!ply && next->alpha >= next->beta) {
                    if(n->alpha > pstack->alpha)
                        pstack->alpha = n->alpha;
                }
            }
        }
    }

UPDATE:
    /*update node's score*/
    l_lock(n->lock);
    if(rollout_type == ALPHABETA)
        n->score = score;
    else
        n->score = (n->score * (n->visits - 1) + score * visits) /
                      ((n->visits - 1)  + visits);
    n->visits += visits;
    l_unlock(n->lock);

FINISH:
    /*clear busy flag, also virtual visits*/
    l_lock(n->lock);
    n->visits--;
    n->clear_busy();
    l_unlock(n->lock);
}
void SEARCHER::search_mc() {
    Node* root = root_node;
    Node* best = Node::Max_pv_select(root);
    double pfrac = 0,score;
    int visits;
    int oalpha = pstack->alpha;
    int obeta = pstack->beta;
    
    /*do rollouts*/
    while(true) {

        /*simulate*/
        play_simulation(root,score,visits);

        /*search stopped*/
        if(abort_search || stop_searcher)
            break;
        
        /*check for exit conditions*/
        if(rollout_type == ALPHABETA) {

            /*best move failed low*/
            if(use_ab
                && best->alpha >= best->beta
                && -best->score <= oalpha
                ) {
                root_failed_low = 3;
                break;
            }

            /*exit when window closes*/
            if(use_ab &&
                (root->alpha >= root->beta || 
                 root->alpha >= obeta ||
                 root->beta  <= oalpha)
                ) {
                break;
            }

            /*negamax*/
            if(!use_ab &&
                root->alpha > -MATE_SCORE &&
                root->beta < MATE_SCORE)
                break;
        }

        /*book keeping*/
        if(processor_id == 0) {

            /*check for messages from other hosts*/
#ifdef CLUSTER
#   ifndef THREAD_POLLING
            if((root->visits % 1000) == 0) {
                processors[processor_id]->idle_loop();
            }
#   endif
#endif  
            /*rank 0*/
            if(true CLUSTER_CODE(&& PROCESSOR::host_id == 0)) { 
                /*check quit*/
                if(root->visits % 1000 == 0) {
                    check_quit();
                    double frac = double(get_time() - start_time) / 
                            chess_clock.search_time;
                    dUCTK = UCTKmax - frac * (UCTKmax - UCTKmin);
                    if(dUCTK < UCTKmin) dUCTK = UCTKmin;
                    if(frac - pfrac >= 0.1) {
                        pfrac = frac;
                        if(rollout_type == MCTS)
                            print_pv(root->score);
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
        best = Node::Max_pv_select(root);
        root->score = -best->score;
        root_score = root->score;
        pstack->best_score = root_score;
        print_pv(root_score);
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
        for(i = 0;i < 2;i++) {
            if(hstack[hply - 1 - i].hash_key == root_key) {
                found = true;
                break;
            }
        }
        if(found && reuse_tree) {
            MOVE move;
            for(j = i;j >= 0;--j) {
                move = hstack[hply - 1 - j].move;
                root = Node::reclaim(root,&move);
                if(!root) break;
            }
        } else {
            Node::reclaim(root);
            root = 0;
        }
    }
    if(!root) {
        print_log("[Tree not found]\n");
        root = Node::allocate();
    } else {
        print_log("[Tree found : visits %d wins %6d]\n",
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
        root->visits += pstack->count;
    }
    root_key = hash_key;

    /*only have root child*/
    if(!freeze_tree && frac_freeze_tree == 0)
        freeze_tree = true;
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
    } else if(!strcmp(command, "mcts_strategy_depth")) {
        mcts_strategy_depth = atoi(commands[command_num++]);
    } else if(!strcmp(command, "montecarlo")) {
        montecarlo = atoi(commands[command_num++]);
    } else if(!strcmp(command, "treeht")) {
        UBMP32 ht = atoi(commands[command_num++]);
        UBMP32 size = ht * ((1024 * 1024) / sizeof(Node));
        print("treeht %d X %d = %.1f MB\n",size,sizeof(Node),
            (size * sizeof(Node)) / double(1024 * 1024));
        Node::max_tree_nodes = size;
    } else {
        return false;
    }
    return true;
}
void print_mcts_params() {
    print("feature option=\"UCTKmin -spin %d 0 100\"\n",int(UCTKmin*100));
    print("feature option=\"UCTKmax -spin %d 0 100\"\n",int(UCTKmax*100));
    print("feature option=\"reuse_tree -check %d\"\n",reuse_tree);
    print("feature option=\"backup_type -combo *MINMAX AVERAGE\"\n");
    print("feature option=\"frac_alphabeta -spin %d 0 100\"\n",int(frac_alphabeta*100));
    print("feature option=\"frac_freeze_tree -spin %d 0 100\"\n",int(frac_freeze_tree*100));
    print("feature option=\"mcts_strategy_depth -spin %d 0 100\"\n",mcts_strategy_depth);
    print("feature option=\"montecarlo -check %d\"\n",montecarlo);
    print("feature option=\"treeht -spin %d 0 131072\"\n",
        int((Node::max_tree_nodes * sizeof(Node)) / double(1024*1024)));
}
