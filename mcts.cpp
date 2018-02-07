#include "scorpio.h"

/*mcts parameters*/
static double  UCTKmax = 0.3;
static double  UCTKmin = 0.3;
static double  dUCTK = UCTKmax;
static int  reuse_tree = 1;
static int  evaluate_depth = 0;
static int  backup_type = MINMAX;
static double frac_alphabeta = 1.0; 
static int  mcts_strategy_depth = 15;

int montecarlo = 0;
int rollout_type = ALPHABETA;

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

void Node::rank_children(Node* n,int alpha,int beta) {
    Node* current = n->child;
    while(current) {
        rank_children(current,-beta,-alpha);
        
        /*find rank of child*/
        int rank = 1;
        double val = current->uct_wins;
        Node* cur = n->child;
        while(cur) {
            double val1 = cur->uct_wins;
            if(val1 > val) rank++;
            cur = cur->next;
        }
        current->rank = rank;
        /*end rank*/

        current = current->next;
    }
    /*reset bounds*/
    n->alpha = alpha;
    n->beta = beta;
    n->flag = ACTIVE;
}

static inline float logistic(float eloDelta) {
    static const double K = -log(10.0) / 400.0;
    return 1 / (1 + exp(K * eloDelta));
}

Node* Node::Max_UCB_select(Node* n) {
    double logn = log(double(n->uct_visits));
    double uct, bvalue = -1;
    Node* current, *bnode = 0;

    current = n->child;
    while(current) {
        if(!current->move) {
            current->flag = INVALID;
        } else {
            uct = logistic(current->uct_wins) +
                  dUCTK * sqrt(logn / current->uct_visits);
            if(uct > bvalue) {
                bvalue = uct;
                bnode = current;
            }
        }
        current = current->next;
    }

    return bnode;
}
Node* Node::Max_AB_select(Node* n,int alpha,int beta,bool try_null) {
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
            if(use_ab) uct = current->uct_wins;
            else uct = -current->uct_visits;

            if(!current->move) {
                if(try_null) {
                    uct = MATE_SCORE - 1;
                    current->flag = ACTIVE;
                } else {
                    uct = -MATE_SCORE;
                    current->flag = INVALID;
                }
            }
            if(uct > bvalue) {
                bvalue = uct;
                bnode = current;
            }
        }

        current = current->next;
    }

    return bnode;
}
Node* Node::Max_score_select(Node* n) {
    double bvalue = -MAX_NUMBER, uct;
    Node* current = n->child, *bnode = n->child;

    while(current) {
        if(current->is_active()) {
            uct = current->uct_wins;
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
            if(current->uct_visits > max_visits) {
                max_visits = current->uct_visits;
                best = current;
            }
        }
        current = current->next;
    }
    return best;
}
Node* Node::Best_select(Node* n) {
    if(backup_type == AVERAGE)
        return Max_visits_select(n);
    else
        return Max_score_select(n);  
}
void SEARCHER::create_children(Node* n) {
    /*lock*/
    l_lock(n->lock);
    if(n->child) {
        l_unlock(n->lock);
        return;
    }

    /*maximum tree depth*/
    if(ply > Node::maxuct)
        Node::maxuct = ply;

    /*generate and score moves*/
    if(ply)
        generate_and_score_moves(evaluate_depth * UNITDEPTH,-MATE_SCORE,MATE_SCORE);

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
        node->uct_wins = pstack->score_st[i];
        node->uct_visits = 1;
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
    node->uct_wins = -eval();
    POP_NULL();
    node->uct_visits = 1;
    node->alpha = -MATE_SCORE;
    node->beta = MATE_SCORE;
    node->rank = 0;
    last->next = node;
}
void SEARCHER::play_simulation(Node* n, double& result, int& visits) {

    /*virtual loss*/
    l_lock(n->lock);
    n->uct_visits++;
    l_unlock(n->lock);

    nodes++;

    /*uct tree policy*/
    if(!n->child) {

        /*terminal nodes*/
        if(draw()) {
            result = ((scorpio == player) ? -contempt : contempt);
        } else if(bitbase_cutoff()) {
            result = pstack->best_score;
        } else if(ply >= MAX_PLY - 1) {
            result = eval();
        } else if(pstack->alpha > MATE_SCORE - WIN_PLY * ply) {
            result = pstack->alpha;
        } else if(pstack->depth <= 0) {                        //run out of depth
            result = -n->uct_wins;
        } else if(Node::total_nodes >= Node::max_tree_nodes) { //run out of memory
            n->uct_wins = -get_search_score();
            result = -n->uct_wins;
        } else {
            create_children(n);
            if(!n->child) {
                if(hstack[hply - 1].checks)
                    result = -MATE_SCORE + WIN_PLY * (ply + 1);
                else 
                    result = 0;
            } else {
                if(rollout_type == ALPHABETA) {
                    goto SELECT;
                } else  {
                    Node::maxply += (ply + 1);
                    result = n->child->uct_wins;
                    visits = pstack->count;
                    nodes += visits;
                    goto UPDATE;
                }
            }
        }

        /*visits and maxply*/
        visits = 1;
        Node::maxply += ply;

        /*update alpha-beta bounds*/
        if(rollout_type == ALPHABETA) {
            l_lock(n->lock);
            n->alpha = result;
            n->beta = result;
            l_unlock(n->lock);
        }

    } else {

SELECT:
        /*select move*/
        Node* next;
        int eval_score = 0;
        if(rollout_type == ALPHABETA) {
            bool try_null = pstack->depth >= 4 * UNITDEPTH 
                            && (eval_score = eval()) >= pstack->beta;
            next = Node::Max_AB_select(n,-pstack->beta,-pstack->alpha,try_null);
        } else {
            next = Node::Max_UCB_select(n);
        }

        /*could happen in parallel search*/
        if(!next) {
            visits = 1;
            result = -n->uct_wins;
            goto BACKUP;
        }

        /*Determine next alpha-beta bound*/
        int alphac, betac;
        alphac = -pstack->beta;
        betac = -pstack->alpha;
        if(next->alpha > alphac) alphac = next->alpha;
        if(next->beta < betac)    betac = next->beta;

        if(next->move) {
            /*Make move*/
            PUSH_MOVE(next->move);
            pstack->alpha = alphac;
            pstack->beta = betac;
            pstack->depth = (pstack - 1)->depth - UNITDEPTH;
            pstack->search_state = NULL_MOVE;
            /*Next ply depth*/
            if(rollout_type == ALPHABETA) {
                if(use_selective && be_selective_mc(next->rank)) {
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
            play_simulation(next,result,visits);
            /*Undo move*/
            POP_MOVE();
        } else {
            /*Make nullmove*/
            PUSH_NULL();
            pstack->alpha = alphac;
            pstack->beta = alphac + 1;
            pstack->search_state = NORMAL_MOVE;
            /*Next ply depth*/
            pstack->depth = (pstack - 1)->depth - 3 * UNITDEPTH - 
                            (pstack - 1)->depth / 4 -
                            (MIN(3 , (eval_score - (pstack - 1)->beta) / 128) * UNITDEPTH);
            /*Simulate nullmove*/
            play_simulation(next,result,visits);
            /*Undo nullmove*/
            POP_NULL();
        }

BACKUP:
        /*Average/Minmax style backups*/
        if(backup_type == AVERAGE) {
            result = -result;
        } else {
            Node* best = Node::Max_score_select(n);
            result = best->uct_wins;

            /*check for null move fail high*/
            if(!best->move && best->alpha >= best->beta) {
                if(result >= pstack->beta) {
                    l_lock(n->lock);
                    n->alpha = result;
                    n->beta = result;
                    l_unlock(n->lock);
                    goto UPDATE;
                } else {
                    best->flag = Node::INVALID;
                }
            }

            /*update alpha-beta bounds*/
            if(rollout_type == ALPHABETA) {
                Node* current = n->child;
                int alpha = -MATE_SCORE;
                int beta = -MATE_SCORE;
                while(current) {
                    if(current->is_active()) {
                        if(-current->beta > alpha) alpha = -current->beta;
                        if(-current->alpha > beta) beta = -current->alpha;
                    }
                    current = current->next;
                }
                l_lock(n->lock);
                n->alpha = alpha;
                n->beta = beta;
                l_unlock(n->lock);
            }
        }
    }

UPDATE:
    /*update node's score*/
    l_lock(n->lock);
    n->uct_visits--;
    if(rollout_type == ALPHABETA)
        n->uct_wins = -result;
    else
        n->uct_wins = (n->uct_wins * n->uct_visits - result * visits) /
                      (n->uct_visits  + visits);
    n->uct_visits += visits;
    l_unlock(n->lock);
}
void SEARCHER::search_mc() {
    Node* root = root_node;
    double pfrac = 0,result;
    int visits;
    int oalpha = pstack->alpha;
    int obeta = pstack->beta;

    while(!abort_search) {

        /*exit when window closes*/
        if(use_ab && rollout_type == ALPHABETA &&
            (root->alpha >= root->beta || 
             root->alpha >= obeta ||
             root->beta  <= oalpha)
            ) {
            break;
        }

        /*negamax*/
        if(!use_ab && rollout_type == ALPHABETA &&
            root->alpha > -MATE_SCORE &&
            root->beta < MATE_SCORE)
            break;

        /*simulate*/
        play_simulation(root,result,visits);
        
        /*exit when fail low is resolved at root*/
        root_score = -root->uct_wins;
        if(root_failed_low && root_score > oalpha)
            break;

        /*book keeping*/
        if(processor_id == 0) {

            /*check for messages from other hosts*/
#ifdef CLUSTER
#   ifndef THREAD_POLLING
            if((root->uct_visits % 1000) == 0) {
                processors[processor_id]->idle_loop();
            }
#   endif
#endif  
            /*rank 0*/
            if(true CLUSTER_CODE(&& PROCESSOR::host_id == 0)) { 
                /*check quit*/
                if(root->uct_visits % 1000 == 0) {
                    check_quit();
                    double frac = double(get_time() - start_time) / 
                            chess_clock.search_time;
                    dUCTK = UCTKmax - frac * (UCTKmax - UCTKmin);
                    if(dUCTK < UCTKmin) dUCTK = UCTKmin;
                    if(frac - pfrac >= 0.1) {
                        pfrac = frac;
                        print_pv(-root->uct_wins);
                    }
                    /*Switching rollouts type*/
                    if(rollout_type == ALPHABETA && frac_alphabeta != 1.0 
                        && frac > frac_alphabeta) {
                        print("Switching rollout type to MCTS.\n");
                        rollout_type = MCTS;
                        search_depth = search_depth + mcts_strategy_depth;
                        pstack->depth = search_depth * UNITDEPTH;
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
    }

    /*print pv*/
    print_pv(-root->uct_wins);
    pstack->best_score = -root->uct_wins;
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
        print_log("[Tree found : visits %d wins %6.2f%%]\n",
            root->uct_visits,root->uct_wins);

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
        root->uct_visits += pstack->count;
    }
    root_key = hash_key;
}
/*
* Search parameters
*/
bool check_mcts_params(char** commands,char* command,int& command_num) {
    if(!strcmp(command, "UCTKmin")) {
        UCTKmin = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "UCTKmax")) {
        UCTKmax = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "evaluate_depth")) {
        evaluate_depth = atoi(commands[command_num++]);
    } else if(!strcmp(command, "reuse_tree")) {
        reuse_tree = atoi(commands[command_num++]);
    } else if(!strcmp(command, "backup_type")) {
        backup_type = atoi(commands[command_num++]);
    } else if(!strcmp(command, "frac_alphabeta")) {
        frac_alphabeta = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "mcts_strategy_depth")) {
        mcts_strategy_depth = atoi(commands[command_num++]) / 100.0;
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
    print("feature option=\"evaluate_depth -spin %d 0 100\"\n",evaluate_depth);
    print("feature option=\"reuse_tree -check %d\"\n",reuse_tree);
    print("feature option=\"backup_type -combo *MINMAX AVERAGE NODETYPE\"\n");
    print("feature option=\"frac_alphabeta -spin %d 0 100\"\n",int(frac_alphabeta*100));
    print("feature option=\"mcts_strategy_depth -spin %d 0 100\"\n",mcts_strategy_depth);
    print("feature option=\"montecarlo -check %d\"\n",montecarlo);
    print("feature option=\"treeht -spin %d 0 131072\"\n",
        int((Node::max_tree_nodes * sizeof(Node)) / double(1024*1024)));
}
