#include "scorpio.h"

static double  UCTKmax = 0.3;
static double  UCTKmin = 0.1;
static double  dUCTK = UCTKmax;
static int  reuse_tree = 1;
static int  evaluate_depth = 0;

static inline float logistic(float eloDelta) {
    return 1 / (1 + pow(10.0f,-eloDelta / 400.0));
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
            uct = current->uct_wins / (2.0 * current->uct_visits);
        else
            uct = current->prior;
        uct += dUCTK * sqrt(logn / (current->uct_visits + 1));

        if(uct > bvalue) {
            bvalue = uct;
            bnode = current;
        }

        current = current->next;
    }

    return bnode;
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

    /*generate moves*/
    pstack->count = 0;
    gen_all();
    int legal_moves = 0;
    for(int i = 0;i < pstack->count; i++) {
        MOVE& move = pstack->move_st[i];
        PUSH_MOVE(move);
        if(attacks(player,plist[COMBINE(opponent,king)]->sq)) {
            POP_MOVE();
            continue;
        }
        POP_MOVE();
        pstack->move_st[legal_moves] = move;
        legal_moves++;
    }
    pstack->count = legal_moves;

    /*score moves*/
    evaluate_search(evaluate_depth * UNITDEPTH);

    /*add nodes to tree*/
    Node* last = n;
    for(int i = 0;i < pstack->count; i++) {
        Node* node = Node::allocate();
        node->move = pstack->move_st[i];
        node->prior = 2 * logistic(pstack->score_st[i]);
        if(last == n) last->child = node;
        else last->next = node;
        last = node;
    }
    n->nchildren = legal_moves;

    /*unlock*/
    l_unlock(n->lock);
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
            result = 1;
        } else if(ply >= MAX_PLY - 1) {
            result = 2 - n->prior;
        } else {
            create_children(n);
            if(!n->child) {
                if(hstack[hply - 1].checks)
                    result = 0;
                else 
                    result = 1;
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
        result = (2 - result);
    }

    /*update node's score*/
    l_lock(n->lock);
    n->uct_wins += (2 - result);
    l_unlock(n->lock);

    return (result);
}
void SEARCHER::evaluate_search(int depth) {

    int rootf = root_failed_low;
    root_failed_low = 1;
    for(int i = 0;i < pstack->count; i++) {
        pstack->current_move = pstack->move_st[i];
        PUSH_MOVE(pstack->current_move);

        search_calls++;

        pstack->node_type = PV_NODE;
        pstack->search_state = NORMAL_MOVE;
        pstack->extension = 0;
        pstack->reduction = 0;
        pstack->alpha = -MATE_SCORE;
        pstack->beta = MATE_SCORE;
        pstack->depth = depth;
        pstack->qcheck_depth = UNITDEPTH; 
        ::search(processors[processor_id]);

        POP_MOVE();
        pstack->score_st[i] = -(pstack+1)->best_score;
    }
    root_failed_low = rootf;

    for(int i = 0;i < pstack->count; i++)
        pstack->sort(i,pstack->count);
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
            root->uct_visits,(root->uct_wins + 1) * 100.0 / (2.0 * (root->uct_visits + 1)));
    }
    if(!root->child) 
        create_children(root);
    root_key = hash_key;
}
/*
* Find best move using MCTS
*/
MOVE SEARCHER::mcts() {

    /*init*/
    int i;
    ply = 0;
    pstack = stack + 0;
    abort_search = 0;
    root_failed_low = 0;

    /*set search time*/
    if(!chess_clock.infinite_mode)
        chess_clock.set_stime(hply);

    /*fen*/
    char fen[MAX_FEN_STR];
    get_fen(fen);
    print_log("%s\n",fen);

    start_time = get_time();

#ifdef PARALLEL
    /*wakeup threads*/
    for(i = 1;i < PROCESSOR::n_processors;i++) {
        processors[i]->state = WAIT;
    }
    while(PROCESSOR::n_idle_processors < PROCESSOR::n_processors - 1);
#endif

    /* manage tree*/
    search_calls = 0;
    Node* root = root_node;
    manage_tree(root,root_key);
    if(!root->child) 
        create_children(root);
    root_node = root;
    Node::maxply = 0;
    Node::maxuct = 0;

    if(root->child->next) {

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

    } else {
        root->uct_visits++;
        root->uct_wins++;
    }


#ifdef PARALLEL
    /*park threads*/
    for(i = 1;i < PROCESSOR::n_processors;i++) {
        processors[i]->state = PARK;
    }
#endif

#ifdef CLUSTER
    /*park hosts*/
    if(PROCESSOR::host_id == 0 && use_abdada_cluster) {
        for(i = 1;i < PROCESSOR::n_hosts;i++)
            PROCESSOR::ISend(i,PROCESSOR::QUIT);
    }
#endif

    /*last pv*/
    print_mc_pv(root);

    /* print result*/
    int time_used = MAX(1,get_time() - start_time);
    int nps = int(root->uct_visits / (time_used / 1000.0f));
    print("nodes = %d depth = %d/%d time = %dms pps = %d visits = %d search calls = %d\n",
        Node::total,Node::maxply / root->uct_visits,
        Node::maxuct,time_used,nps,root->uct_visits,search_calls);

    /*best move*/
    Node* best = Node::MAX_select(root_node,1,MAX_PLY);
    stack[0].pv[0] = best->move;
    stack[0].pv_length = 1;
    if(best->child) {
        best = Node::MAX_select(best,0);
        stack[0].pv[1] = best->move;
        stack[0].pv_length = 2;
    }

    /*return*/
    return stack[0].pv[0];
}
/*
* IO
*/
void Node::print_xml(Node* n,int depth) {
    char mvstr[32];
    mov_str(n->move,mvstr);

    print_log("<node depth=\"%d\" move=\"%s\" visits=\"%d\" wins=\"%e\" prior=\"%e\">\n",
        depth,mvstr,n->uct_visits,(n->uct_wins+2)*100/(2.0*(n->uct_visits+1)),n->prior);

    Node* current = n->child;
    while(current) {
        print_xml(current,depth+1);
        current = current->next;
    }

    print_log("</node>\n");
}
void SEARCHER::extract_pv(Node* n) {
    unsigned int max_visits = 0;
    Node* current = n->child, *best = 0;
    while(current) {
        if(current->uct_visits > max_visits) {
            max_visits = current->uct_visits;
            best = current;
        }
        current = current->next;
    }
    if(best) {
        pstack->pv[ply] = best->move;
        pstack->pv_length = ply+1;
        ply++;
        extract_pv(best);
        ply--;
    }
}
void SEARCHER::print_mc_pv(Node* n) {
    MOVE  move;
    char mv_str[64];
    int i,j;

    /*extract pv from tree*/
    extract_pv(n);

    /*print info*/
    double winf = 1 - n->uct_wins / (2.0 * n->uct_visits);
    int score = int(-400 * log10((1 - winf)/ winf));
    print("%d %d %d " FMT64 " ",stack[0].pv_length, 
        score,(get_time() - start_time) / 10,n->uct_visits);

    /*print what we have*/
    for(i = 0;i < stack[0].pv_length;i++) {
        move = stack[0].pv[i];
        strcpy(mv_str,"");
        mov_str(move,mv_str);
        print(" %s",mv_str);

        PUSH_MOVE(move);
    }
    /*undo moves*/
    for (j = 0; j < i ; j++)
        POP_MOVE();
    print("\n");
}
Node* Node::MAX_select(Node* root,int output,int max_depth,int depth) {
    char str[16];
    int bvisits = -1;
    Node* bnode = 0;
    Node* current = 0;
    int considered = 0,total = 0;

    current = root->child;
    while(current) {
        if(int(current->uct_visits) > bvisits) {
            bvisits = current->uct_visits;
            bnode = current;
        }
        current = current->next;
    }

    current = root->child;
    while(current) {
        if(current->uct_visits && (depth == 0 || bnode == current) ) {
            considered++;
            if(depth <= max_depth && bnode == current) {
                MAX_select(current,output,max_depth,depth+1);
            }
            if(output) {
                mov_str(current->move,str);
                for(int i = 0;i < depth;i++)
                    print_log("\t");
                print_log("%d %2d.%7s  | %6.2f%%  %6d | %6.2f%%\n",
                    depth+1,
                    total+1,
                    str,
                    current->uct_wins * 100 / (2.0 * current->uct_visits),
                    current->uct_visits,
                    current->prior * 100 / 2.0
                    );
            }
        }
        total++;

        current = current->next;
    }

    if(depth == 0 && output) {
            mov_str(bnode->move,str);
            print_log("Bestmove : %s from %d out of %d moves [%.2f%%]\n",
                str, considered,total,considered * 100.0f / total);
    }

    return bnode;
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