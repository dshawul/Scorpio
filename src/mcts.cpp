#include <random>
#include "scorpio.h"

/*mcts parameters*/
static unsigned int  cpuct_base = 75610;
static float  cpuct_factor = 3.48;
static float  cpuct_init = 0.84;
static float  policy_temp = 2.15;
static float  policy_temp_m = 2.15;
static float  policy_temp_e = 2.15;
static float  fpu_red = 0.33;
static int    fpu_is_loss = 0;
static float  fpu_red_m = 0.33;
static int    fpu_is_loss_m = 0;
static float  fpu_red_e = 0.33;
static int    fpu_is_loss_e = 0;
static float  cpuct_init_root_factor = 1.0;
static float  policy_temp_root_factor = 1.0;
static int  reuse_tree = 1;
static int  backup_type_setting = MIX_VISIT;
static int  backup_type = backup_type_setting; 
static float frac_freeze_tree = 1.0;
static float frac_abrollouts = 0.2;
static int  alphabeta_depth = 1;
static int  evaluate_depth = 0;
static int virtual_loss = 1;
static unsigned int visit_threshold = 800;
static std::random_device rd;
static std::mt19937 mtgen(rd());
static float noise_frac = 0.25;
static float noise_alpha = 0.3;
static float noise_beta = 1.0;
static int temp_plies = 30;
static float rand_temp = 1.0;
static float rand_temp_delta = 0.0;
static float rand_temp_end = 0.0;
static const float winning_threshold = 0.9;
static const int low_visits_threshold = 100;
static const int node_size = 
    (sizeof(Node) + 32 * (sizeof(uint16_t) + sizeof(uint16_t)));
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
static int select_formula = 0;
static int filter_quiet = 0;
static int max_collisions = 0;
static int max_terminals = 0;
static float max_collisions_ratio = 0.25;
static float max_terminals_ratio = 2.0;
static float rms_power = 1.4;
static float insta_move_factor = 0.0;
int  mcts_strategy_depth = 30;
int train_data_type = 0;

int montecarlo = 0;
int rollout_type = ALPHABETA;
bool freeze_tree = false;
float frac_abprior = 0.3;
float frac_alphabeta = 0.0;

int ensemble = 0;
static float ensemble_setting = 0;
int ensemble_type = 0;
std::atomic_int turn_off_ensemble = {0};
static std::atomic_int n_collisions = {0};
static std::atomic_int n_terminals = {0};

/*Nodes and edges of tree*/
std::vector<Node*> Node::mem_[MAX_CPUS];
std::vector<uint16_t*>  Edges::mem_[MAX_CPUS][MAX_MOVES_NN >> 3];
std::atomic_uint Node::total_nodes = {0};
unsigned int Node::max_tree_nodes = 0;
unsigned int Node::max_tree_depth = 0;
unsigned int Node::sum_tree_depth = 0;

Node* Node::allocate(int id) {
    Node* n;
    if(mem_[id].empty()) {
        static const int MEM_INC = 1024;
        aligned_reserve<Node>(n, MEM_INC);
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
        if(!current->is_dead())
            reclaim(current,id);
        current = current->next;
    }
    Edges::reclaim(n->edges,id);
    mem_[id].push_back(n);
    l_add(total_nodes,-1);
}

void Edges::allocate(Edges& edges, int id, int sz_) {
    const int szi = (((sz_ - 1) >> 3) + 1);
    const int sz =  szi << 3;

    if(sz >= MAX_MOVES_NN) {
        int64_t* n;
        aligned_reserve<int64_t>(n, sz >> 1);
        edges.data = (uint16_t*)(n);
        return;
    } else if(mem_[id][szi].empty()) {
        int64_t* n;
        static const int MEM_INC = 128;
        aligned_reserve<int64_t>(n, (sz * MEM_INC) >> 1);

        std::vector<uint16_t*>& vec = mem_[id][szi];
        vec.reserve(MEM_INC);
        for(int i = 0;i < MEM_INC;i++)
            vec.push_back((uint16_t*)(n) + 2 * i * sz);
    }

    std::vector<uint16_t*>& vec = mem_[id][szi];
    edges.data = vec.back();
    vec.pop_back();
}

void Edges::reclaim(Edges& edges, int id) {
    if(!edges.count)
        return;
    const int szi = (((edges.count - 1) >> 3) + 1);
    const int sz =  szi << 3;
    if(sz >= MAX_MOVES_NN) {
        int64_t* n = (int64_t*)edges.data;
        aligned_free<int64_t>(n);
    } else {
        std::vector<uint16_t*>& vec = mem_[id][szi];
        vec.push_back(edges.data);
    }
}

/*add child nodes*/
static void add_node(Node* n, Node* node) {
    if(!n->child)
        n->child = node;
    else {
        Node* current = n->child, *prev = 0;
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
    node->beta = MATE_SCORE;
    node->rank = idx + 1;
    node->prior = 0;
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
    node->prior = 0;
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
                val += MAX_NUMBER;

            /*find rank of current child*/
            int rank = 1;
            Node* cur = n->child;
            while(cur) {
                if(cur->move && cur != current) {
                    double val1 = -cur->score;
                    if(cur->is_pvmove())
                        val1 += MAX_NUMBER;

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

void Node::convert_score(Node* n) {
    Node* current = n->child;
    while(current) {
        if(!current->is_dead())
            convert_score(current);
        current = current->next;
    }
    if(rollout_type == MCTS)
        n->score = logit(n->score);
    else
        n->score = logistic(n->score);
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

float Node::compute_Q(float fpu, unsigned int collision_lev, bool has_ab) {
    float uct;
    if(!visits) {
        uct = fpu;
    } else {
        uct = 1 - score;
        if(uct >= winning_threshold)
            uct += 100;
        unsigned int vvst = collision_lev * get_busy();
        if(vvst)
            uct += (-uct * vvst) / (vvst + visits + 1);
    }
    if(has_ab) {
        float uctp = 1 - prior;
        uct = 0.5 * ((1 - frac_abprior) * uct +
                    frac_abprior * uctp +
                    MIN(uct,uctp));
    }
    if(is_create()) uct *= 0.1;
    return uct;
}

float Node::compute_fpu(int ply) {
    float fpu = 0.0;
    if(!ply || visits > 10000)
        fpu = 1.0;            //fpu = win
    else if(!fpu_is_loss) {
        float fpur = fpu_red; //fpu = reduction
        if((ply == 1) || (visits > 3200))
            fpur = 0;         //fpu reduction = 0
        else
            fpu = v_pol_sum;  //sum of children policies
        fpu = score - fpur * sqrt(fpu);
    } else {
        fpu = (1 - fpu_is_loss) / 2.0; //fpu is loss or win
    }
    return fpu;
}

float Node::compute_policy_sum_reverseKL(Node* n, float factor, float fpu, float alpha) {
    float reg_policy_sum = 0.f, policy_sum = 0.f, Q;

    Node* current = n->child;
    while(current) {
        if(current->move) {
            if(!current->is_dead()) {
                policy_sum += current->policy;

                Q = current->Q;
                current->reg_policy = factor * current->policy / (alpha - Q);
                reg_policy_sum += current->reg_policy;
            } else {
                policy_sum += current->policy;
                reg_policy_sum += current->reg_policy;
            }
        }
        current = current->next;
    }

    Q = fpu;
    reg_policy_sum += factor * (1 - policy_sum) / (alpha - Q);
    return reg_policy_sum;
}

float Node::compute_regularized_policy_reverseKL(Node* n, float factor, float fpu, bool has_ab) {

    /*approaching collision limit?*/
    unsigned int collision_lev = virtual_loss;
    if(SEARCHER::use_nn && max_collisions >= 3) {
        if(n_collisions >= ((3 * max_collisions) >> 2))
            collision_lev <<= 2;
        else if(n_collisions >= (max_collisions >> 1))
            collision_lev <<= 1;
    }

    /*find alpha_min and alpha_max*/
    float alpha, alpha_min = -100, alpha_max = -100;
    Node* current = n->child;
    while(current) {
        if(current->move && !current->is_dead()) {

            current->Q = current->compute_Q(fpu, collision_lev, has_ab);

            float a = current->Q;
            float b = a + factor;
            a += factor * current->policy;

            if(a > alpha_min) alpha_min = a;
            if(b > alpha_max) alpha_max = b;
        }
        current = current->next;
    }

    int idx = n->edges.get_children();
    if(idx < n->edges.count) {
        float a = fpu;
        float b = a + factor;
        a += factor * n->edges.get_score(idx);

        if(a > alpha_min) alpha_min = a;
        if(b > alpha_max) alpha_max = b;
    }

    /*find alpha and regularized policy using dichotomous search*/
    static const float eps = 0.01, eps_alpha = 0.001;
    static const int max_iters = 100;
    float sum, sum_min, sum_max;
    sum_min = Node::compute_policy_sum_reverseKL(n,factor,fpu,alpha_min);
    sum_max = Node::compute_policy_sum_reverseKL(n,factor,fpu,alpha_max);
    for(int i = 0; i < max_iters; i++) {
        alpha = 0.5 * (alpha_min + alpha_max);
        sum = Node::compute_policy_sum_reverseKL(n,factor,fpu,alpha);

        if(fabs(1 - sum) <= eps ||
           fabs(alpha_min - alpha_max) <= eps_alpha ||
           fabs(sum_min - sum_max) <= eps)
            break;
        if(sum < 1) {
            alpha_max = alpha;
            sum_max = sum;
        } else {
            alpha_min = alpha;
            sum_min = sum;
        }
#if 0
        if(i == max_iters - 1) {
            print("[%3d] ALPHA %f %f = %f SUM %.2f %.2f = %.2f ERR %.4f VIS %d\n",i,
                alpha_min,alpha_max,alpha,sum_min,sum_max,sum,fabs(1-sum),n->visits);
            if(i == max_iters - 1) exit(0);
        }
#endif
    }

    float ret = factor / (alpha - fpu);
    return ret;
}

Node* Node::ExactPi_select(Node* n, bool has_ab, SEARCHER* ps) {
    bool is_root = (ps->ply == 0);
    float dCPUCT = cpuct_init * (is_root ? cpuct_init_root_factor : 1.0f) +
                    cpuct_factor * logf((n->visits + cpuct_base + 1.0) / cpuct_base);
    float factor = dCPUCT * (float)(sqrt(double(n->visits))) / (n->edges.get_children() + n->visits);

    /*compute fpu*/
    float fpu = n->compute_fpu(ps->ply);

    /*compute regularized policy for selection*/
    float factor_unvisited = Node::compute_regularized_policy_reverseKL(n,factor,fpu,has_ab);

    /*select*/
    float uct, bvalue = -10;
    Node* current = n->child, *bnode = 0;
    while(current) {
        if(current->move && !current->is_dead()) {

            uct = current->reg_policy - float(double(current->visits) / n->visits);

            if(forced_playouts && is_selfplay && is_root && current->visits > 0) {
                unsigned int n_forced = sqrt(2 * current->policy * n->visits);
                if(current->visits < n_forced) uct += 5;
            }

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
            uct = factor_unvisited * n->edges.get_score(idx);
            if(uct > bvalue) {
                bnode = n->add_child(ps->processor_id, idx,
                    ps->decode_move(n->edges.get_move(idx)),
                    n->edges.get_score(idx),
                    1 - n->edges.score);
                if(ps->ply > 1)
                    n->v_pol_sum += n->edges.get_score(idx);
                n->edges.inc_children();
                n->edges.clear_create();
                return bnode;
            }
        }
        n->edges.clear_create();
    }

    return bnode;
}

Node* Node::Max_UCB_select(Node* n, bool has_ab, SEARCHER* ps) {
    bool is_root = (ps->ply == 0);
    float dCPUCT = cpuct_init * (is_root ? cpuct_init_root_factor : 1.0f) +
                    cpuct_factor * logf((n->visits + cpuct_base + 1.0) / cpuct_base);
    float factor;

    /*compute fpu*/
    float fpu = n->compute_fpu(ps->ply);

    /*Alphazero or UCT formula*/
    if(select_formula == 0)
        factor = dCPUCT * float(sqrt(double(n->visits)));
    else
        factor = dCPUCT * float(sqrt(log(double(n->visits))));

    /*approaching collision limit?*/
    unsigned int collision_lev = virtual_loss;
    if(SEARCHER::use_nn && max_collisions >= 3) {
        if(n_collisions >= ((3 * max_collisions) >> 2))
            collision_lev <<= 2;
        else if(n_collisions >= (max_collisions >> 1))
            collision_lev <<= 1;
    }

    /*select*/
    float uct, bvalue = -10;
    Node* current = n->child, *bnode = 0;
    while(current) {
        if(current->move && !current->is_dead()) {

            /*compute Q considering fpu and virtual loss*/
            uct = current->compute_Q(fpu, collision_lev, has_ab);

            if(select_formula == 0)
                uct += factor * (current->policy / (current->visits + 1));
            else
                uct += factor * sqrt(current->policy / (current->visits + 1));

            if(forced_playouts && is_selfplay && is_root && current->visits > 0) {
                unsigned int n_forced = sqrt(2 * current->policy * n->visits);
                if(current->visits < n_forced) uct += 5;
            }

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
            if(select_formula == 0)
                uct = fpu + factor * n->edges.get_score(idx);
            else
                uct = fpu + factor * sqrt(n->edges.get_score(idx));
            if(uct > bvalue) {
                bnode = n->add_child(ps->processor_id, idx,
                    ps->decode_move(n->edges.get_move(idx)),
                    n->edges.get_score(idx),
                    1 - n->edges.score);
                if(ps->ply > 1)
                    n->v_pol_sum += n->edges.get_score(idx);
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
    bool search_by_rank, SEARCHER* ps
    ) {

    /*lock*/
    while(!n->edges.try_create())
        t_pause();

    /*select*/
    float bvalue = -MAX_NUMBER, uct;
    int alphac, betac;
    Node* current = n->child, *bnode = 0;
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
            /*Discourage selection of busy and children-in-creation node*/
            uct -= virtual_loss * current->get_busy() * 10;
            if(current->is_create()) uct *= 0.25;
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
                bnode = n->add_child(ps->processor_id, idx,
                    ps->decode_move(n->edges.get_move(idx)),
                    n->edges.get_score(idx),
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

    double temp;
    if(hply < temp_plies)
        temp = rand_temp - (hply >> 1) * rand_temp_delta / ((temp_plies >> 1) - 1);
    else
        temp = rand_temp_end;

    count = 0;
    current = n->child;
    while(current) {
        if(current->move) {
            node_pt.push_back(current);
            val = current->visits;
            if(n->visits < (unsigned)low_visits_threshold) 
                val += (low_visits_threshold - n->visits) * current->policy;
            else
                val++;
            val = pow(val, 1.0 / temp);
            freq.push_back(1000 * val);
            count++;
        }
        current = current->next;
    }
    if(count) {
        std::discrete_distribution<int> dist(freq.begin(),freq.end());
        int idx = dist(mtgen);
        bnode = node_pt[idx];
#if 0
        for(int i = 0; i < count; ++i) {
            char mvs[16];
            mov_str(pstack->move_st[i], mvs);
            print("%c%3d. %6s %d\n",(i == idx) ? '*':' ',i,mvs,freq[i]);
        }
#endif
    }

    return bnode;
}

int SEARCHER::Random_select_ab() {
    std::vector<int> freq;
    int bidx = 0;

    static const float scale = 12.f;
    double temp;
    if(hply < temp_plies)
        temp = rand_temp - (hply >> 1) * rand_temp_delta / ((temp_plies >> 1) - 1);
    else
        temp = rand_temp_end;

    int maxs = -MATE_SCORE, avgs = 0;
    for(int i = 0;i < pstack->count; i++) {
        int score = pstack->score_st[i];
        if(score > maxs) maxs = score;
        avgs += score;
    }
    avgs /= pstack->count;

    int delta = 0 - avgs;
    if(maxs >= 800)
        delta = 0;
    else if(maxs + delta >= 800)
        delta = 800 - maxs;

    for(int i = 0;i < pstack->count; i++) {
        float pp = logistic(pstack->score_st[i] + delta) * scale;
        freq.push_back( 100 * exp(pp / temp) );
    }

    if(pstack->count) {
        std::discrete_distribution<int> dist(freq.begin(),freq.end());
        bidx = dist(mtgen);
#if 0
        for(int i = 0; i < pstack->count; ++i) {
            char mvs[16];
            mov_str(pstack->move_st[i], mvs);
            print("%c%3d. %6s %7d %d\n",(i == bidx) ? '*':' ',
                i,mvs,pstack->score_st[i],freq[i]);
        }
#endif
    }

    return bidx;
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
                    if(n->visits < (unsigned)low_visits_threshold) 
                        val += (low_visits_threshold - n->visits) * current->policy;
                    else
                        val++;
                } else {
                    if(current->visits)
                        val = (rollout_type == MCTS) ? 
                            1 - current->score : -current->score;
                    else
                        val = bvalue - 1;

                    if(has_ab && (rollout_type == MCTS)) {
                        double valp = 1 - current->prior;
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
            tvalue += current->score * current->visits;
            tvisits += current->visits;
        }
        current = current->next;
    }
    if(tvisits == 0) {
        tvalue = 1 - n->score;
        tvisits = 1;
    }
    return tvalue / tvisits;
}
float Node::Avg_score_mem(Node* n, float score, int visits) {
    float sc = n->score;
    sc += double(score - sc) * visits / (n->visits + visits);
    return sc;
}
#if 1
float Node::Rms_score_mem(Node* n, float score, int visits) {
#define signof(x) (((x) > 0) ? 1 : -1)
    float sc, sc1;

    sc = 2 * n->score - 1;
    sc = signof(sc) * pow(fabs(sc), rms_power);

    sc1 = 2 * score - 1;
    sc1 = signof(sc1) * pow(fabs(sc1), rms_power);

    sc += double(sc1 - sc) * visits / (n->visits + visits);

    sc = signof(sc) * pow(fabs(sc), 1.0 / rms_power);
    sc = sc / 2.0 + 0.5;

    return sc;
#undef signof
}
#else
float Node::Rms_score_mem(Node* n, float score, int visits) {
    float sc = pow(n->score, rms_power);
    float sc1 = pow(score, rms_power);
    sc += double(sc1 - sc) * visits / (n->visits + visits);
    sc = pow(sc, 1.0 / rms_power);
    return sc;
}
#endif
void Node::Backup(Node* n,float& score,int visits) {
    if(rollout_type == MCTS) {
        float pscore = score;
        /*Compute parent's score from children*/
        if(backup_type == CLASSIC)
            score = Avg_score_mem(n,score,visits);
        else if(backup_type == MIX_VISIT) {
            if(n->visits > visit_threshold)
                score = 1 - Max_visits_score(n);
            else
                score = 1 - Avg_score(n);
        }
        else if(backup_type == RMS) {
            if(n->visits > visit_threshold)
                score = Rms_score_mem(n,score,visits);
            else
                score = Avg_score_mem(n,score,visits);
        }
        else if(backup_type == AVERAGE)
            score = 1 - Avg_score(n);
        else {
            if(backup_type == MINMAX || backup_type == MINMAX_MEM)
                score = 1 - Min_score(n);
            else if(backup_type == MIX  || backup_type == MIX_MEM)
                score = 1 - (Min_score(n) + 3 * Avg_score(n)) / 4;

            if(backup_type >= MINMAX_MEM)
                score = Avg_score_mem(n,score,visits);
        }
        n->update_score(score);
        if(backup_type == CLASSIC || backup_type == RMS)
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
void Node::BackupLeaf(Node* n,float& score) {
    if(rollout_type == MCTS) {
        n->update_score(score);
    } else if(n->alpha < n->beta) {
        n->update_score(score);
        n->set_bounds(score,score);
    }
}

void SEARCHER::create_children(Node* n) {

    /*generate and score moves*/
    n->score = generate_and_score_moves(-MATE_SCORE,MATE_SCORE);

    /*add edges tree*/
    n->edges.score = n->score;
    if(pstack->count) {
        Edges::allocate(n->edges, processor_id, pstack->count);
        for(int i = 0; i < pstack->count; i++) {
            n->edges.set_move(i, encode_move(pstack->move_st[i]));
            n->edges.set_score(i, pstack->count, pstack->score_st[i]);
        }
    }

    /*add children if we are at root node*/
    int nleaf = ply ? 0 : pstack->count;
    float rscore = (rollout_type == MCTS) ? 1 - n->score : -n->score;
    for(int i = 0; i < nleaf; i++) {
        float* pp =(float*)&pstack->score_st[i];
        n->add_child(processor_id, i,
            pstack->move_st[i], *pp, rscore);
    }

    /*add nullmove*/
    if(rollout_type != MCTS
        && use_nullmove
        && pstack->count
        && ply > 0
        && !hstack[hply - 1].checks
        && piece_c[player]
        && hstack[hply - 1].move != 0
        ) {
        n->add_null_child(processor_id, -n->score);
    }

    /*set number of children now*/
    n->edges.n_children = nleaf;
    n->edges.count = pstack->count;
}

/*prefetch nodes recursively and evaluate*/
void SEARCHER::prefetch_nodes(int idx) {
    gen_all_legal();
    if(pstack->count > 0) {
        if(idx >= pstack->count) {
            int chosen_move_id = idx % pstack->count;
            idx /= pstack->count;
            PUSH_MOVE(pstack->move_st[chosen_move_id]);
            if(ply < MAX_PLY - 1 
                && !draw() 
                && !bitbase_cutoff()
            ) {
                prefetch_nodes(idx);
            } else {
                gen_all_legal();
                probe_neural(true);
            }
            POP_MOVE();
            return;
        } else if(idx < 0)
            idx = 0;

        PUSH_MOVE(pstack->move_st[idx]);
        gen_all_legal();
        probe_neural(true);
        POP_MOVE();
        return;
    }
    probe_neural(true);
}

/*handle collision nodes*/
bool SEARCHER::handle_collisions(Node* n) {
    if(rollout_type == MCTS &&
        l_add(n_collisions,1) <= max_collisions)
        ;
    else {
        prefetch_nodes(n->get_busy() - 2);
        l_add(n_collisions,-1);
        return true;
    }
    return false;
}


/*handle terminal nodes*/
bool SEARCHER::handle_terminals(Node* n) {
    if(rollout_type == MCTS &&
        l_add(n_terminals,1) <= max_terminals)
        ;
    else {
        gen_all_legal();
        probe_neural(true);
        l_add(n_terminals,-1);
        return true;
    }
    return false;
}

void SEARCHER::play_simulation(Node* n, float& score, int& visits) {

    nodes++;
    visits = 1;
    bool leaf = true;

    /*set busy flag*/
    n->inc_busy();

#if 0
    unsigned int nvisits = n->visits;
#endif

    /*Terminal nodes*/
    if(ply) {
        /*if node has children, it is not terminal*/
        if(ply == 1 || !n->edges.count) {
            /*Draw*/
            if(draw()) {
                if(rollout_type == MCTS)
                    score = 0.5;
                else
                    score = ((scorpio == player) ? -contempt : contempt);
                goto BACKUP_LEAF;
            /*bitbases*/
            } else if(bitbase_cutoff()) {
                if(rollout_type == MCTS)
                    score = logistic(pstack->best_score);
                else
                    score = pstack->best_score;
                goto BACKUP_LEAF;
            /*Reached max plies and depth*/
            } else if(ply >= MAX_PLY - 1) {
                score = n->score;
                goto BACKUP_LEAF;
            }
        }
        /*AB specific*/
        if(rollout_type == ALPHABETA) {
            /*max depth*/
            if(pstack->depth <= 0) {
                score = n->score;
                goto BACKUP_LEAF;
            }
            /*mate distance pruning*/
            if(n->alpha > MATE_SCORE - WIN_PLY * (ply + 1)) {
                score = n->alpha;
                goto BACKUP_LEAF;
            }
        }
    }

    /*No children*/
    if(!n->edges.count) {

        /*run out of memory for nodes*/
        if(rollout_type == MCTS
            && Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes
            ) {
            abort_search = 1;
            print_info("Maximum number of nodes reached.\n");
            visits = 0;
            goto FINISH;
        } else if(rollout_type == ALPHABETA && 
             (
             Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes       
             || freeze_tree
             || pstack->depth <= alphabeta_depth
             )
            ) {
            if(Node::total_nodes  + MAX_MOVES >= Node::max_tree_nodes &&
                !freeze_tree && processor_id == 0) {
                freeze_tree = true;
                print_info("Maximum number of nodes reached.\n");
            }
            score = search_ab();
            if(stop_searcher || abort_search)
                goto FINISH;
        /*create children*/
        } else {

            if(n->try_create()) {
                if(!n->edges.count)
                    create_children(n);
                n->clear_create();
            } else {
                visits = 0;
                if(use_nn && handle_collisions(n))
                    visits = -1;
                goto FINISH;
            }

            if(!n->edges.count) {
                if(rollout_type == MCTS) {
                    if(hstack[hply - 1].checks)
                        score = 0;
                    else
                        score = 0.5;
                } else {
                    if(hstack[hply - 1].checks)
                        score = -MATE_SCORE + WIN_PLY * (ply + 1);
                    else
                        score = ((scorpio == player) ? -contempt : contempt);
                }
                goto BACKUP_LEAF;
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
            handle_terminals(n);

    /*Has children*/
    } else {
SELECT:
        /*select move*/
        Node* next = 0;
        if(rollout_type == ALPHABETA) {
            bool try_null = pstack->node_type != PV_NODE
                            && pstack->depth >= 4
                            && n->score >= pstack->beta;
            bool search_by_rank = (pstack->node_type == PV_NODE);

            next = Node::Max_AB_select(n,-pstack->beta,-pstack->alpha,
                try_null,search_by_rank,this);
        } else {
            bool has_ab_ = ((n == root_node) && has_ab);
            if(select_formula >= 2)
                next = Node::ExactPi_select(n, has_ab_,this);
            else
                next = Node::Max_UCB_select(n, has_ab_,this);
        }

        /*This could happen in parallel search*/
        if(!next) {
            if(use_nn)
                handle_terminals(n);
            score = n->score;
            goto FINISH;
        }

        /*AB rollout*/
        int next_node_t, alphac, betac, try_scout;

        if(rollout_type == ALPHABETA) {
            /*Determine next node type*/
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
            alphac = -pstack->beta;
            betac = -pstack->alpha;
            if(next->alpha > alphac) alphac = next->alpha;
            if(next->beta < betac)    betac = next->beta;
            /*scout search*/
            try_scout = (alphac + 1 < betac &&
                  ABS(betac) != MATE_SCORE &&
                  pstack->node_type == PV_NODE && 
                  next_node_t == CUT_NODE);
        }

        if(next->move) {

            /*Make move*/
            PUSH_MOVE(next->move);
RESEARCH:
            pstack->depth = (pstack - 1)->depth - 1;
            pstack->search_state = NULL_MOVE;

            if(rollout_type == ALPHABETA) {
                /*AB window*/
                if(try_scout) {
                    pstack->alpha = betac - 1;
                    pstack->beta = betac;
                } else {
                    pstack->alpha = alphac;
                    pstack->beta = betac;
                }
                pstack->node_type = next_node_t;
                /*Next ply depth*/
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

            if(rollout_type == MCTS)
                score = 1 - score;
            else
                score = -score;
            if(stop_searcher || abort_search)
                goto FINISH;

            /*Research if necessary when window closes*/
            if(rollout_type == ALPHABETA
                && visits > 0
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

            if(visits <= 0)
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
            pstack->depth = (pstack - 1)->depth - 3 - 
                            (pstack - 1)->depth / 4 -
                            (MIN(3 , (n->score - (pstack - 1)->beta) / 128));
            /*Simulate nullmove*/
            play_simulation(next,score,visits);

            if(rollout_type == MCTS)
                score = 1 - score;
            else
                score = -score;
            if(stop_searcher || abort_search)
                goto FINISH;

            /*Undo nullmove*/
            POP_NULL();

            /*Nullmove cutoff*/
            if(visits > 0 && next->alpha >= next->beta) {
                if(score >= pstack->beta)
                    n->set_bounds(score,score);
            }
            goto FINISH;
            /*end null move*/
        }
BACKUP:
        /*Backup score and bounds*/
        leaf = false;
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
    if(visits > 0) {
        n->update_visits(visits);
        /*update depth stats*/
        if(leaf) {
            if(ply > (int)Node::max_tree_depth)
                Node::max_tree_depth = (ply + 1);
            Node::sum_tree_depth += (ply + 1);
        }
    }
    n->dec_busy();
}

void SEARCHER::unboost_policy() {
#ifdef CLUSTER
    if(use_abdada_cluster && PROCESSOR::n_hosts > 1) {
        for(unsigned int i = 0; i < PROCESSOR::pv_tree_nodes.size(); i++) {
            Node* nd = PROCESSOR::pv_tree_nodes[i];
            if(!nd) break;
            nd->policy /= 2;
        }
        PROCESSOR::pv_tree_nodes[0] = 0;
    }
#endif
}
void SEARCHER::boost_policy() {
#ifdef CLUSTER
    if(use_abdada_cluster && PROCESSOR::n_hosts > 1) {
        unboost_policy();
        int cnt = 0;
        for(int j = 0;j < PROCESSOR::n_hosts;j++) {
            int depth = 0;
            Node* current = root_node->child;
            while(current) {
                if(current->move
                    && depth < PROCESSOR::node_pvs[j].pv_length
                    && current->move == PROCESSOR::node_pvs[j].pv[depth]
                    ) {
                    depth++;
                    PROCESSOR::pv_tree_nodes[cnt++] = current;
                    PROCESSOR::pv_tree_nodes[cnt] = 0;
                    current->policy *= 2;
                    current = current->child;
                } else
                    current = current->next;
            }
        }
    }
#endif
}

/*compute kld b/n network policy and current simulation pi*/
double SEARCHER::compute_kld() {
    Node* current = root_node->child;
    double kld = 0;
    while(current) {
        if(current->visits) {
            float pre_noise_policy = root_node->edges.get_score(current->rank - 1);
            kld -= pre_noise_policy *
                  log(current->visits / ((pre_noise_policy + 1e-6) * root_node->visits));
        }
        current = current->next;
    }
    return kld;
}

void SEARCHER::compute_time_factor(int rscore) {
    for(int i = 0; i < 2; i++) {
        if(rscore <= -150)
            time_factor *= 3.0;
        else if(rscore <= -120)
            time_factor *= 2.3;
        else if(rscore <= -70)
            time_factor *= 1.6;
        else if(rscore <= -30)
            time_factor *= 1.3;
        else if(rscore >= 100)
            time_factor *= 2.0;
        else if(rscore >= 55)
            time_factor *= 1.6;
        else if(rscore >= 35)
            time_factor *= 1.3;
        else if(ABS(rscore) > 10)
            time_factor *= 1.1;
        if(first_search)
            break;
        rscore = rscore - old_root_score;
    }
    /*Extend time to resolve best move differences 
     from different hosts*/
#ifdef CLUSTER
    if(use_abdada_cluster && PROCESSOR::n_hosts > 1
        && PROCESSOR::host_id == 0
        ) {
        for(int i = 0; i < PROCESSOR::n_hosts; i++) {
            MOVE move = PROCESSOR::node_pvs[i].pv[0];
            if(move && move != stack[0].pv[0]) {
                time_factor *= 2;
                return;
            }
        }
    }
#endif
}
void SEARCHER::check_mcts_quit(bool single) {

    /*only mcts*/
    if(rollout_type != MCTS)
        return;

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
            if(1 - current->score > 1 - bnval->score)
                bnval = current;
        }
        current = current->next;
    }

    /*determine time factor*/
    time_factor = 1.0;
    if(bnval != bnvis)
        time_factor *= 1.3;
    int rscore = logit(root_node->score);
    compute_time_factor(rscore);

    /*calculate remain visits*/
    int remain_visits;
    if(chess_clock.max_visits == MAX_NUMBER) {
        int time_used = MAX(1,get_time() - start_time);
        int remain_time = time_factor * chess_clock.search_time - time_used;
        float imf = insta_move_factor + (time_factor - 1.0) / 2;
        imf = MIN(imf, 0.9);
        remain_visits = (remain_time / (double)time_used) * 
            (root_node->visits - (root_node_reuse_visits * imf));
    } else {
        remain_visits = chess_clock.max_visits - 
            (root_node->visits - (root_node_reuse_visits * insta_move_factor));
    }
#if 0
    print_info("insta %f vistis %d reuse %d remain %d\n",
        insta_move_factor,root_node->visits, root_node_reuse_visits, remain_visits);
#endif

    /*prune root nodes*/
    int visdiff;
    float factor;

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
    float pfrac = 0,score;
    int visits;
    int oalpha = pstack->alpha;
    int obeta = pstack->beta;
    unsigned int ovisits = root->visits;
    unsigned int visits_poll;

    /*poll input after this many playouts*/
    if(chess_clock.max_visits != MAX_NUMBER) {
        if(is_selfplay) visits_poll = 100;
        else visits_poll = chess_clock.max_visits / 40;
    } else if(use_nn) {
        visits_poll = 4 * PROCESSOR::n_processors;
    } else {
        unsigned int np = single ? 1 : PROCESSOR::n_processors;
        visits_poll = MAX(200 * np, average_pps / 40);
        visits_poll = MIN(1000 * np, visits_poll);
    }
    prev_kld = -1;

    /*wait until all idle processors are awake*/
    if(!single) {
        static std::atomic_int t_count = {0};
        int p_t_count = l_add(t_count,1);
        if(p_t_count == PROCESSOR::n_processors - 1)
            t_count = 0;
        while(t_count > 0 && t_count < PROCESSOR::n_processors) {
            t_yield();
            if(SEARCHER::use_nn) t_sleep(SEARCHER::delay);
        }
    }

    /*Set alphabeta rollouts depth*/
    int ablimit = (1 - frac_abrollouts) * pstack->depth;
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
        if(visits)
            l_add(SEARCHER::playouts,1);

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
            /*rank 0*/
            if(true CLUSTER_CODE(&& PROCESSOR::host_id == 0)) { 
                /*check quit*/
                if(root->visits - ovisits >= visits_poll) {
                    ovisits = root->visits;
                    check_quit();

                    float frac = 1;
                    if(chess_clock.max_visits != MAX_NUMBER)
                        frac = double(root->visits) / chess_clock.max_visits;
                    else 
                        frac = double(get_time() - start_time) / chess_clock.search_time;

                    if(frac - pfrac >= 0.05) {
                        pfrac = frac;
                        if(rollout_type == MCTS) {
                            extract_pv(root);
                            root_score = logit(root->score);
                            print_pv(root_score);
                            search_depth++;
                            boost_policy();
                        }
                    }
                    
                    if((is_selfplay || (frac >= 0.2)) && !chess_clock.infinite_mode)
                        check_mcts_quit(single);

                    if(ensemble && (frac > ensemble_setting)) {
                        print_info("Turning off ensemble.\n");
                        t_sleep(1);
                        turn_off_ensemble = 1;
                    }

                    /*stop growing tree after some time*/
                    if(rollout_type == ALPHABETA && !freeze_tree && frac_freeze_tree < 1.0 &&
                        frac >= frac_freeze_tree * frac_alphabeta) {
                        freeze_tree = true;
                        print_info("Freezing tree.\n");
                    }
                }
            }
        }
    }

    /*update statistics of parent*/
    if(!single && master) {
        l_lock(lock_smp);
        l_lock(master->lock);
        update_master(1);
        l_unlock(master->lock);
        l_unlock(lock_smp);
    } else if(!single && !abort_search && !stop_searcher) {
        bool failed = (-best->score <= oalpha) || 
                      (-best->score >= obeta);
        if(!failed)
            best = Node::Best_select(root,has_ab);

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
        root_score = logit(root->score);
        if(!single)
            print_pv(root_score);
        search_depth++;
    }
    /*clear dead*/
    current = root->child;
    while(current) {
        current->clear_dead();
        current = current->next;
    }
    /*unboost*/
    unboost_policy();
}
/*
Traverse tree in parallel
*/
static std::vector<Node*> gc[MAX_CPUS+1];

void CDECL gc_thread_proc(void* seid_) {
    int* seid = (int*)seid_;
    for(int proc_id = seid[0]; proc_id < seid[1]; proc_id++) {
        for(unsigned int i = 0; i < gc[proc_id].size(); i++) {
            Node::reclaim(gc[proc_id][i],proc_id);
        }
    }
}
void CDECL rank_reset_thread_proc(void* seid_) {
    int* seid = (int*)seid_;
    for(int proc_id = seid[0]; proc_id < seid[1]; proc_id++) {
        for(unsigned int i = 0; i < gc[proc_id].size(); i++) {
            Node::rank_children(gc[proc_id][i]);
            Node::reset_bounds(gc[proc_id][i]);
        }
    }
}

void CDECL convert_score_thread_proc(void* seid_) {
    int* seid = (int*)seid_;
    for(int proc_id = seid[0]; proc_id < seid[1]; proc_id++) {
        for(unsigned int i = 0; i < gc[proc_id].size(); i++) {
            Node::convert_score(gc[proc_id][i]);
        }
    }
}

void Node::parallel_job(Node* n, PTHREAD_PROC func, bool recursive) {
    int ncores = PROCESSOR::n_cores;
    int nprocs = PROCESSOR::n_processors;
    int T = 0, S = MAX(1,n->visits / (8 * nprocs)),
                 V = nprocs / ncores;

    for(int i = 1;i < PROCESSOR::n_processors;i++)
        PROCESSOR::park(i);

    Node::split(n, gc, S, T);

    int* seid = new int[2 * ncores];
    std::thread* tid = new std::thread[ncores];

    if(!recursive)
        gc[0].push_back(n);
    else {
        gc[nprocs].push_back(n);
        seid[0] = nprocs;
        seid[1] = nprocs + 1;
        tid[0] = t_create(*func,&seid[0]);
        t_join(tid[0]);
    }

    for(int i = 0; i < ncores; i++) {
        seid[2*i+0] = i * V;
        seid[2*i+1] = (i == ncores - 1) ? nprocs : ((i + 1) * V);
        tid[i] = t_create(*func,&seid[2*i]);
    }
    for(int i = 0; i < ncores; i++)
        t_join(tid[i]);

    delete[] seid;
    delete[] tid;

    for(int i = 0; i < nprocs;i++) {
        for(unsigned int j = 0; j < gc[i].size(); j++) {
            gc[i][j]->clear_dead();
        }
        gc[i].clear();
    }

    for(int i = 1;i < PROCESSOR::n_processors;i++)
        PROCESSOR::wait(i);
}

/*
Manage search tree
*/
void SEARCHER::manage_tree(bool single) {
    unsigned int prev_root_visits = 0;
    if(root_node) {

        unsigned int s_total_nodes = Node::total_nodes;
        prev_root_visits = root_node->visits;

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
            else Node::parallel_job(oroot, gc_thread_proc);
            if(new_root) {
                root_node = new_root;

                Node* n = root_node;
                float rscore = (rollout_type == MCTS) ? 1 - n->edges.score : -n->edges.score;
                for(int i = n->edges.n_children; i < n->edges.count; i++) {
                    n->add_child(processor_id, i,
                        decode_move(n->edges.get_move(i)),
                        n->edges.get_score(i),
                        rscore);
                    n->edges.inc_children();
                }
            }
            else
                root_node = 0;
        } else {
            if(single) Node::reclaim(root_node,processor_id);
            else Node::parallel_job(root_node, gc_thread_proc);
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
        root_node_reuse_visits = 0;
        Node::sum_tree_depth = 0;
    } else {
        print_log("# [Tree-found : visits %d score %f]\n",
            unsigned(root_node->visits),float(root_node->score));

        root_node_reuse_visits = root_node->visits;
        Node::sum_tree_depth *= (0.5 * double(root_node->visits) / prev_root_visits);
        playouts += root_node->visits;

        /*remove null move from root*/
        Node* current = root_node->child;
        if(current && current->move == 0) {
            root_node->child = current->next;
            Node::reclaim(current,0);
        }
    }
    Node::max_tree_depth = 0;

    /*no alphabeta*/
    if(frac_alphabeta == 0) {
        rollout_type = MCTS;
        search_depth = MIN(search_depth + mcts_strategy_depth, MAX_PLY - 2);
    }

    /*create root node*/
    if(!root_node->child) {
        create_children(root_node);
        root_node->visits++;
    }
    root_node->set_pvmove();
    root_key = hash_key;

    /*only have root child*/
    if(!freeze_tree && frac_freeze_tree == 0)
        freeze_tree = true;
    if(frac_alphabeta > 0) {
        if(rollout_type == MCTS)
            Node::parallel_job(root_node,convert_score_thread_proc);
        rollout_type = ALPHABETA;
    }

    /*backup type*/
    backup_type = backup_type_setting;

    /*max collisions and terminals*/
    max_collisions = 
        (int)(PROCESSOR::n_processors * max_collisions_ratio / SEARCHER::n_devices);
    max_terminals = 
        (int)(PROCESSOR::n_processors * max_terminals_ratio);

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
float SEARCHER::generate_and_score_moves(int alpha, int beta) {
    float rscore = 0;

    /*generate moves here*/
    gen_all_legal();

    /*filter egbb moves*/
    if(egbb_is_loaded && all_man_c <= MAX_EGBB) {
        int score, bscore;

        if(pstack->count && probe_bitbases(bscore)) {
            int bscorem = (bscore >= WIN_SCORE) ? WIN_SCORE :
                       ((bscore <= -WIN_SCORE) ? -WIN_SCORE : 0);
            int legal_moves = 0;
            for(int i = 0;i < pstack->count; i++) {
                MOVE& move = pstack->move_st[i];
                PUSH_MOVE(move);
                if(probe_bitbases(score)) {
                    score = -score;
                    int scorem = (score >= WIN_SCORE) ? WIN_SCORE :
                       ((score <= -WIN_SCORE) ? -WIN_SCORE : 0);
                    if(scorem < bscorem) {
                        POP_MOVE();
                        continue;
                    }
                }
                POP_MOVE();
                pstack->move_st[legal_moves] = pstack->move_st[i];
                pstack->score_st[legal_moves] = pstack->score_st[i];
                legal_moves++;
            }
            pstack->count = MAX(1,legal_moves);
        }
    }

    /*compute move probabilities*/
    if(pstack->count) {

        /*value head only with uniform policy*/
        if(evaluate_depth == -5) {
            if(!use_nn) {
                rscore = eval();
                if(rollout_type == MCTS)
                    rscore = logistic(rscore);
            } else {
                rscore = probe_neural();
                if(rollout_type == ALPHABETA)
                    rscore = logit(rscore);
                n_collisions = 0;
                n_terminals = 0;
            }
            const float uniform_pol = 1.0 / pstack->count;
            for(int i = 0;i < pstack->count; i++) {
                float* p = (float*)&pstack->score_st[i];
                *p = uniform_pol;
            }
            return rscore;
        }

        /*both value and policy heads*/
        const float my_policy_temp = 1.0f /
                (policy_temp * (ply ? 1 : policy_temp_root_factor));

        if(!use_nn) {

            bool save = skip_nn;
            skip_nn = true;
            evaluate_moves(evaluate_depth);
            skip_nn = save;

            rscore = pstack->score_st[0];
            if(rollout_type == MCTS)
                rscore = logistic(rscore);

            if(!montecarlo) return rscore;

            /*normalize policy*/
            static const float scale = 25.f;
            float total = 0.f;
            for(int i = 0;i < pstack->count; i++) {
                float* const p = (float*)&pstack->score_st[i];
                float pp = logistic(pstack->score_st[i]) * scale;
                *p = exp(pp * my_policy_temp);
                total += *p;
            }
            total = 1.0f / total;
            for(int i = 0;i < pstack->count; i++) {
                float* const p = (float*)&pstack->score_st[i];
                *p *= total;
            }
        } else {
            rscore = probe_neural();
            if(rscore > 1.0) rscore = 1.0;
            else if(rscore < 0.0) rscore = 0.0;
            if(rollout_type == ALPHABETA)
                rscore = logit(rscore);
            n_collisions = 0;
            n_terminals = 0;

            if(!montecarlo) return rscore;

            /*max legal moves cap*/
            const int mp = pstack->score_st[MAX_MOVES_NN - 1];
            for(int i = MAX_MOVES_NN; i < pstack->count; i++)
                pstack->score_st[i] = mp;

            /*find minimum and maximum policy values*/
            float total = 0.f, maxp = -100, minp = 100;
            for(int i = 0;i < pstack->count; i++) {
                float* const p = (float*)&pstack->score_st[i];
                MOVE& move = pstack->move_st[i];
                if((nn_type != 1) && is_prom(move)) {
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
                static const float margin[2][2] = {{0.36,0.49},{-100.0,-7.0}};
                for(int i = 0;i < pstack->count; i++) {
                    float* const p = (float*)&pstack->score_st[i];
                    MOVE& move = pstack->move_st[i];
                    int sfifty = fifty;

                    PUSH_MOVE(move);
                    gen_all_legal();
                    if(!pstack->count) {
                        if(hstack[hply - 1].checks) {      //mate
                            *p = 5 * maxp;
                        } else if(rscore >= margin[rollout_type][0]) {         //stale-mate
                            *p = minp;
                        }
                    } else if(rscore >= margin[rollout_type][1] && draw(1)) {   //repetition & 50-move draws
                        *p = minp;
                    } else if(sfifty > 0 && fifty == 0) {  //encourage progress
                        *p += sfifty * (maxp - minp) / 50;
                    }
                    POP_MOVE();
                }
            }
            /*normalize policy*/
            for(int i = 0;i < pstack->count; i++) {
                float* const p = (float*)&pstack->score_st[i];
                *p = expf( (*p - maxp) * my_policy_temp );
                total += *p;
            }
            total = 1.0f / total;
            for(int i = 0;i < pstack->count; i++) {
                float* const p = (float*)&pstack->score_st[i];
                float pp = *p * total;
                if(pp < 2 * min_policy_value)
                    pp = MAX(pp, min_policy_value + pp * 0.125f);
                *p = pp;
            }

            pstack->sort_all();
        }
    }
    return rscore;
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
   char  fen[MAX_FEN_STR];
} *PTRAIN;

/*multiple worker threads*/
void SEARCHER::launch_worker_threads() {

    /*wakeup threads*/
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        PROCESSOR::wait(i);
#if defined(CLUSTER)
    PROCESSOR::set_mt_state(WAIT);
#endif

    /*attach helper processor here once*/
    l_lock(lock_smp);
    for(int i = 1;i < PROCESSOR::n_processors;i++) {
        attach_processor(i);
        processors[i]->state = GOSP;
    }
    l_unlock(lock_smp);

    /*montecarlo search*/
    worker_thread();

    /*wait till all helpers become idle*/
    idle_loop_main();

    /*park threads*/
    for(int i = 1;i < PROCESSOR::n_processors;i++)
        PROCESSOR::park(i);
#if defined(CLUSTER)
    PROCESSOR::set_mt_state(PARK);
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
void SEARCHER::get_train_data(float& value, int& nmoves, int* moves,
    float* probs, float* scores, int& bestm
    ) {

    /*value*/
    if(montecarlo)
        value = root_node->score;
    else
        value = logistic(pstack->best_score);
    if(player == black) 
        value = 1 - value;

    /*value head only returns here*/
    if(train_data_type == 3)
        return;

    /*AB search*/
    if(!montecarlo) {
        static const float scale = 12.f;
        double total = 0.f;
        for(int i = 0;i < pstack->count; i++) {
            MOVE& move = pstack->move_st[i];
            int score = pstack->score_st[i];
            float pp = logistic(pstack->score_st[i]);
            moves[i] = compute_move_index(move, 0);
            scores[i] = pp;
            pp = exp(pp * scale);
            probs[i] = pp;
            total += pp;
        }
        for(int i = 0;i < pstack->count; i++)
            probs[i] /= total;

        nmoves = pstack->count;
        MOVE& move = pstack->best_move;
        bestm = compute_move_index(move, 0);
        return;
    }

    /*policy target pruning*/
    Node* current;
    float factor, max_uct = 0;

    if(policy_pruning) {
        float dCPUCT = cpuct_init +
            log((root_node->visits + cpuct_base + 1.0) / cpuct_base), uct;
        factor = dCPUCT * sqrt(double(root_node->visits));

        current = root_node->child;
        while(current) {
            uct = 1 - current->score;
            uct += current->policy * factor / (current->visits + 1);
#if 0
            char mvstr[16];
            mov_str(current->move,mvstr);
            print("%3d. %7s %9.2f %9.2f %9.4f\n",
                current->rank,mvstr,current->policy,
                1 - current->score,uct);
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
        MOVE& move = current->move;
        /*skip underpromotions*/
        if(is_prom(move) && 
            PIECE(m_promote(move)) != queen) {
            current = current->next;
            continue;
        }
        /*probabilities*/
        if(train_data_type == 0 || train_data_type == 2) {
            val = current->visits;

            if(policy_pruning) {
                val = ((current->policy * factor) /
                    (max_uct - (1 - current->score))) - 1;
                if(val < 0) val = 0;
#if 0
                char mvstr[16];
                mov_str(move,mvstr);
                print("%3d. %7s %9.2f %9.2f %9d %9d\n",
                    current->rank,mvstr,current->policy,
                    1 - current->score,current->visits,int(val));
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
            val = (1 - current->score);
            scores[cnt] = val;
        }
        moves[cnt] = compute_move_index(move, 0);
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

            if(train_data_type == 3)
                bcount += sprintf(&buffer[bcount], " %f ",
                    ptrn->value);
            else {
                bcount += sprintf(&buffer[bcount], " %f %d ", 
                    ptrn->value, ptrn->nmoves);

                for(int i = 0; i < ptrn->nmoves; i++) {
                    if(train_data_type == 2)
                        bcount += sprintf(&buffer[bcount], "%d %f %f ", 
                            ptrn->moves[i], ptrn->probs[i], ptrn->scores[i]);
                    else if(train_data_type == 1)
                        bcount += sprintf(&buffer[bcount], "%d %f ", 
                            ptrn->moves[i], ptrn->scores[i]);
                    else
                        bcount += sprintf(&buffer[bcount], "%d %f ", 
                            ptrn->moves[i], ptrn->probs[i]);
                }

                bcount += sprintf(&buffer[bcount], "%d", ptrn->bestm);
            }

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
    static std::atomic_int wins = {0}, losses = {0}, draws = {0};
    static const int vlimit = chess_clock.max_visits * frac_sv_low, phply = ply;
    MOVE move;
    char* buffer = new char[4096 * MAX_HSTACK];
    PTRAIN trn = new TRAIN[MAX_HSTACK];

    unsigned start_t = get_time();
    unsigned limit = 0;

    static float average_plies = 0;
    static float average_npm = 0;
    float local_average_npm;

    while(true) {

        local_average_npm = 0;
        limit = 0;

        while(true) {

            /*game ended*/
            int res, score;
            if(egbb_is_loaded && all_man_c <= MAX_EGBB && probe_bitbases(score)) {
                if(score == 0) res = R_DRAW;
                else if(score > 0) res = (player == white) ? R_WWIN : R_BWIN;
                else res = (player == black) ? R_WWIN : R_BWIN;
            } else if(hply >= 4 && -pstack->best_score >= sp_resign_value)
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
                    ngames,int(wins),int(losses),int(draws));

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
                average_plies +=
                    (hply - average_plies) / ngames;
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
                    print("[%d] generated %d games in %.2f min : "
                        "%.2f games/min %.2f nodes/move %.2f plies/game\n",
                        GETPID(), ngames, diff, ngames / diff, average_npm, average_plies);
                }

                return;
            }

            /*do fixed nodes mcts search*/
            generate_and_score_moves(-MATE_SCORE, MATE_SCORE);

            if(montecarlo) {
                manage_tree(true);
                SEARCHER::egbb_ply_limit = 8;
                stop_searcher = 0;
                pstack->depth = search_depth;

                /*katago's playout cap randomization*/
                if(playout_cap_rand && chess_clock.max_visits >= 800) {
                    if(hply & 1);
                    else if(rand() > frac_full_playouts * float(RAND_MAX))
                        limit = vlimit;
                    else
                        limit = 0;
                }

                search_mc(true,limit);
                move = stack[0].pv[0];
                pstack->best_score = logit(root_node->score);
                if(filter_quiet) {
                    Node* bnode = Node::Best_select(root_node,false);
                    pstack->best_move = bnode->move;
                } else
                    pstack->best_move = move;

                local_average_npm += 
                    (root_node->visits - local_average_npm) / (hply + 1);
#if 0
                char mvstr[16];
                mov_str(move,mvstr);
                char fen[256];
                get_fen(fen);
                print("%3d. %7s [%d %5d] = %5d %s\n",
                    hply+1,mvstr,(limit > 0), root_node->visits,
                    int(logit(root_node->score)), fen);
#endif
            } else {
                search_depth = chess_clock.max_sd;
                SEARCHER::egbb_ply_limit = 8;
                stop_searcher = 0;
                nodes = 0;
                qnodes = 0;
                limit = 0;

                evaluate_moves(search_depth - 1);
                pstack->best_score = pstack->score_st[0];
                pstack->best_move = pstack->move_st[0];
                /*pick move to play*/
                int bidx;
                if(hply < temp_plies || rand_temp_end > 0)
                    bidx = Random_select_ab();
                else
                    bidx = 0;
                move = pstack->move_st[bidx];

                local_average_npm += 
                    (nodes - local_average_npm) / (hply + 1);
#if 0
                char mvstr[16];
                mov_str(move,mvstr);
                char fen[256];
                get_fen(fen);
                print("%3d. %7s = %5d %s\n",
                    hply+1,mvstr, pstack->best_score, fen);
#endif
            }

            /*get training data*/
            PTRAIN ptrn = &trn[hply];
            if(filter_quiet &&
                ( is_cap_prom(pstack->best_move) ||
                (hply >= 1 && hstack[hply - 1].checks) ||
                checks(pstack->best_move, hstack[hply].rev_check) )
                ) {
                ptrn->nmoves = -1;
            }
            else if(pcr_write_low || (limit == 0)) {
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
void SEARCHER::worker_thread_all(ParallelFile* pgn, FILE* fw, int task_, bool single) {
    p_pgn = pgn;
    spfile = fw;
    if(task_ <= 3) {
        work_type = 1;
        task = task_;
    } else {
        work_type = 2;
        task = task_ - 4;
    }
    
    if(!single)
        launch_worker_threads();
    else
        worker_thread();
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
    static float policy_temp_o = policy_temp;
    static float fpu_red_o = fpu_red;
    static int  fpu_is_loss_o = fpu_is_loss;

#define SET(p) {                      \
    nn_type = nn_type_##p;            \
    wdl_head = wdl_head_##p;          \
    policy_temp = policy_temp_##p;    \
    fpu_red = fpu_red_##p;            \
    fpu_is_loss = fpu_is_loss_##p;    \
};

    ensemble = (ensemble_setting > 0) ? 1 : 0;
    turn_off_ensemble = 0;

    if(all_man_c <= nn_man_e) {
        if(ensemble) return;

        if(nn_type_e >= 0) {
            print_info("Switching to endgame net!\n");
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
            print_info("Switching to midgame net!\n");
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
    } else if(!strcmp(command, "cpuct_factor")) {
        cpuct_factor = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "cpuct_init")) {
        cpuct_init = atoi(commands[command_num++]) / 100.0;

    } else if(!strcmp(command, "policy_temp")) {
        policy_temp = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_red")) {
        fpu_red = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_is_loss")) {
        fpu_is_loss = atoi(commands[command_num++]);

    } else if(!strcmp(command, "policy_temp_m")) {
        policy_temp_m = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_red_m")) {
        fpu_red_m = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_is_loss_m")) {
        fpu_is_loss_m = atoi(commands[command_num++]);

    } else if(!strcmp(command, "policy_temp_e")) {
        policy_temp_e = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_red_e")) {
        fpu_red_e = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "fpu_is_loss_e")) {
        fpu_is_loss_e = atoi(commands[command_num++]);

    } else if(!strcmp(command, "cpuct_init_root_factor")) {
        cpuct_init_root_factor = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "policy_temp_root_factor")) {
        policy_temp_root_factor = atoi(commands[command_num++]) / 100.0;

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
    } else if(!strcmp(command, "rand_temp_delta")) {
        rand_temp_delta = atoi(commands[command_num++]) / 100.0;
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
    } else if(!strcmp(command, "rms_power")) {
        rms_power = atoi(commands[command_num++]) / 100.0;
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
    } else if(!strcmp(command, "max_collisions_ratio")) {
        max_collisions_ratio = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "max_terminals_ratio")) {
        max_terminals_ratio = atoi(commands[command_num++]) / 100.0;
    } else if(!strcmp(command, "insta_move_factor")) {
        insta_move_factor = atoi(commands[command_num++]) / 100.0;
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
    } else if(!strcmp(command, "select_formula")) {
        select_formula = atoi(commands[command_num++]);
    } else if(!strcmp(command, "filter_quiet")) {
        filter_quiet = is_checked(commands[command_num++]);
    } else if(!strcmp(command, "treeht")) {
        uint32_t ht = atoi(commands[command_num++]);
        uint32_t size = ht * (double(1024 * 1024) / node_size);
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
    print_spin("cpuct_factor",int(cpuct_factor*100),0,1000);
    print_spin("cpuct_init",int(cpuct_init*100),0,1000);

    print_spin("policy_temp",int(policy_temp*100),0,1000);
    print_spin("fpu_red",int(fpu_red*100),-1000,1000);
    print_spin("fpu_is_loss",fpu_is_loss,-1,1);

    print_spin("policy_temp_m",int(policy_temp_m*100),0,1000);
    print_spin("fpu_red_m",int(fpu_red_m*100),-1000,1000);
    print_spin("fpu_is_loss_m",fpu_is_loss_m,-1,1);

    print_spin("policy_temp_e",int(policy_temp_e*100),0,1000);
    print_spin("fpu_red_e",int(fpu_red_e*100),-1000,1000);
    print_spin("fpu_is_loss_e",fpu_is_loss_e,-1,1);

    print_spin("cpuct_init_root_factor",int(cpuct_init_root_factor*100),0,1000);
    print_spin("policy_temp_root_factor",int(policy_temp_root_factor*100),0,1000);

    print_spin("noise_alpha",int(noise_alpha*100),0,100);
    print_spin("noise_beta",int(noise_beta*100),0,100);
    print_spin("noise_frac",int(noise_frac*100),0,100);
    print_spin("temp_plies",temp_plies,0,100);
    print_spin("rand_temp",int(rand_temp*100),0,1000);
    print_spin("rand_temp_delta",int(rand_temp_delta*100),0,1000);
    print_spin("rand_temp_end",int(rand_temp_end*100),0,1000);
    print_check("playout_cap_rand",playout_cap_rand);
    print_spin("frac_full_playouts",int(frac_full_playouts*100),0,100);
    print_spin("frac_sv_low",int(frac_sv_low*100),0,100);
    print_check("pcr_write_low",pcr_write_low);
    print_spin("kld_threshold",int(kld_threshold*1000000),0,1000000);
    print_check("early_stop",early_stop);
    print_spin("train_data_type",train_data_type,0,2);
    print_check("reuse_tree",reuse_tree);
    print_spin("backup_type",backup_type_setting,0,8);
    print_spin("ensemble",int(ensemble_setting*100),0,100);
    print_spin("ensemble_type",ensemble_type,0,2);
    print_spin("rms_power",int(rms_power*100),0,1000);
    print_spin("min_policy_value",int(min_policy_value*1000),0,1000);
    print_spin("frac_alphabeta",int(frac_alphabeta*100),0,100);
    print_spin("frac_freeze_tree",int(frac_freeze_tree*100),0,100);
    print_spin("frac_abrollouts",int(frac_abrollouts*100),0,100);
    print_spin("frac_abprior",int(frac_abprior*100),0,100);
    print_spin("mcts_strategy_depth",mcts_strategy_depth,0,100);
    print_spin("alphabeta_depth",alphabeta_depth,1,100);
    print_spin("evaluate_depth",evaluate_depth,-5,100);
    print_spin("virtual_loss",virtual_loss,0,1000);
    print_spin("max_collisions_ratio",int(max_collisions_ratio*100),0,1000*SEARCHER::n_devices);
    print_spin("max_terminals_ratio",int(max_terminals_ratio*100),0,1000*SEARCHER::n_devices);
    print_spin("insta_move_factor",int(insta_move_factor*100),0,1000);
    print_spin("visit_threshold",visit_threshold,0,1000000);
    print_check("montecarlo",montecarlo);
    print_spin("sp_resign_value",sp_resign_value,0,10000);
    print_check("forced_playouts",forced_playouts);
    print_check("policy_pruning",policy_pruning);
    print_spin("select_formula",select_formula,0,2);
    print_check("filter_quiet",filter_quiet);
    print_spin("treeht",int((Node::max_tree_nodes / double(1024*1024)) * node_size),0,131072);
}
