#include "scorpio.h"

static double  UCTKmax = 0.3;
static double  UCTKmin = 0.1;
static double  dUCTK = UCTKmax;
static int  reuse_tree = 1;
static int  evaluate_depth = 0;

static const double K = -log(10.0) / 400.0;
static inline float logistic(float eloDelta) {
    return 1 / (1 + exp(K * eloDelta));
}

LOCK Node::mem_lock;
std::list<Node*> Node::mem_;
int Node::total = 0;
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
        total += MEM_INC;
    }
    n = mem_.front();
    mem_.pop_front();
    l_unlock(mem_lock);

    n->clear();
    return n;
}
void Node::release(Node* n) {
    l_lock(mem_lock);
    mem_.push_front(n);
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
Node* Node::UCT_select(Node* n) {
    double logn = log(double(n->uct_visits)), uct;
    double bvalue = -1;
    Node* bnode = 0;
    Node* current = n->child;

    while(current) {
        if(current->uct_visits)
            uct = current->uct_wins / current->uct_visits;
        else
            uct = current->prior;
        uct = logistic(uct) + dUCTK * sqrt(logn / (current->uct_visits + 1));

        if(uct > bvalue) {
            bvalue = uct;
            bnode = current;
        }

        current = current->next;
    }

    return bnode;
}
Node* Node::Best_select(Node* n) {
    unsigned int max_visits = 0;
    Node* current = n->child, *best = n->child;
    while(current) {
        if(current->uct_visits > max_visits) {
            max_visits = current->uct_visits;
            best = current;
        }
        current = current->next;
    }
    return best;
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
    generate_and_score_moves(evaluate_depth * UNITDEPTH);

    /*add nodes to tree*/
    add_children(n);

    /*unlock*/
    l_unlock(n->lock);
}
void SEARCHER::add_children(Node* n) {
    Node* last = n;
    for(int i = 0;i < pstack->count; i++) {
        Node* node = Node::allocate();
        node->move = pstack->move_st[i];
        node->prior = pstack->score_st[i];
        if(last == n) last->child = node;
        else last->next = node;
        last = node;
    }
}
double SEARCHER::play_simulation(Node* n) {

    double result;

    /*virtual loss*/
    l_lock(n->lock);
    n->uct_visits++;
    l_unlock(n->lock);

    /*uct tree policy*/
    if(!n->child) {
        if(draw()) {
            result = 0;
        } else if(ply >= MAX_PLY - 1) {
            result = -n->prior;
        } else {
            create_children(n);
            if(!n->child) {
                if(hstack[hply - 1].checks)
                    result = -MATE_SCORE + WIN_PLY * (ply + 1);
                else 
                    result = 0;
            } else {
                n->child->uct_visits++;
                n->child->uct_wins += n->child->prior;
                Node::maxply += (ply + 1);
                result = n->child->prior;
            }
        }
    } else {
        Node* next = Node::UCT_select(n);
        PUSH_MOVE(next->move);
        result = play_simulation(next);
        POP_MOVE();
        result = -result;
    }

    /*update node's score*/
    l_lock(n->lock);
    n->uct_wins += -result;
    l_unlock(n->lock);

    return (result);
}
void SEARCHER::search_mc() {
    double pfrac = 0;
    Node* root = root_node;
    while(!abort_search) {

        play_simulation(root);

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
                if(root->uct_visits % 100 == 0) {
                    check_quit();
                    double frac = double(get_time() - start_time) / 
                            chess_clock.search_time;
                    dUCTK = UCTKmax - frac * (UCTKmax - UCTKmin);
                    if(dUCTK < UCTKmin) dUCTK = UCTKmin;
                    if(frac - pfrac >= 0.05) {
                        print_mc_pv(root);
                        pfrac = frac;
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
}
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
            root->uct_visits,root->uct_wins / (root->uct_visits + 1));
    }
    if(!root->child) 
        create_children(root);
    root_key = hash_key;
}
/*
* Find best move using MCTS
*/
MOVE SEARCHER::mcts() {

    /* manage tree*/
    Node* root = root_node;
    manage_tree(root,root_key);
    root_node = root;
    Node::maxply = 0;
    Node::maxuct = 0;

#ifdef PARALLEL
    /*attach helper processor here once*/
    for(int i = 1;i < PROCESSOR::n_processors;i++) {
        attach_processor(i);
        processors[i]->state = GO;
    }
#endif
    /*search*/
    search_mc();

#ifdef PARALLEL
    /*wait till all helpers become idle*/
    stop_workers();
    while(PROCESSOR::n_idle_processors < PROCESSOR::n_processors - 1)
        t_yield(); 
#endif

    /*return*/
    return stack[0].pv[0];
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
}