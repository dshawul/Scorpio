#include "scorpio.h"

/**
* Distributed search.
*    Scorpio uses a decentralized approach (p2p) where neither memory nor
*    jobs are centrialized. Each host could have multiple processors in which case
*    shared memory search (centralized search with threads) will be used.
*    One process per node will be started by mpirun, then each process 
*    at each node will create enough threads to engage all its processors.
*/

static std::thread threads[MAX_CPUS];

#ifdef CLUSTER

static std::thread message_thread;
static std::atomic_int g_message_id;
static std::atomic_int g_source_id;
static std::atomic_int pongs = {0};
static SPLIT_MESSAGE* global_split;

/**
* Message polling thread for cluster
*/
static void CDECL check_messages(void*) {
    PROCESSOR::message_idle_loop();
}
static std::atomic_int message_thread_state = {PARK};
void PROCESSOR::set_mt_state(int state) {
    message_thread_state = state;
}

/**
* Initialize MPI
*/
void PROCESSOR::init_mpi(int argc, char* argv[]) {
    int namelen,provided,requested = MPI_THREAD_MULTIPLE;
    MPI_Init_thread(&argc, &argv,requested,&provided);
    MPI_Comm_size(MPI_COMM_WORLD, &PROCESSOR::n_hosts);
    MPI_Comm_rank(MPI_COMM_WORLD, &PROCESSOR::host_id);
    MPI_Get_processor_name(PROCESSOR::host_name, &namelen);
    
    /*init thread support*/
    if(provided != requested) {
        static const char* support[] = {
            "MPI_THREAD_SINGLE","MPI_THREAD_FUNNELED",
            "MPI_THREAD_SERIALIZED","MPI_THREAD_MULTIPLE"
        };
        printf("[Warning]: %s not supported. %s provided.\n",
            support[requested],support[provided]);
        printf("[Warning]: Scorpio may hang when run with multiple threads"
            " (including message polling thread).\n");
    }

    printf("Process [%d/%d] on %s : pid %d\n",PROCESSOR::host_id,
        PROCESSOR::n_hosts,PROCESSOR::host_name,GETPID());

    PROCESSOR::Barrier();
}

void PROCESSOR::init_mpi_thread() {
    /*pvs*/
    node_pvs.resize(n_hosts);
    for(int i = 0; i < n_hosts; i++) {
        node_pvs[i].pv_length = 0;
        node_pvs[i].pv[0] = 0;
    }
    pv_tree_nodes.resize(n_hosts * MAX_PLY);
    pv_tree_nodes[0] = 0;
    /*global split point*/
    global_split = new SPLIT_MESSAGE[n_hosts];
    if(n_hosts > 1)
        message_thread = t_create(check_messages,(void*)0);
}
/*
* MPI calls
*/
void PROCESSOR::ISend(int dest,int message) {
    MPI_Request mpi_request;
    MPI_Isend(MPI_BOTTOM,0,MPI_INT,dest,message,MPI_COMM_WORLD,&mpi_request);
}
void PROCESSOR::ISend(int dest,int message,void* data,int size, MPI_Request* rqst) {
    if(rqst)
        MPI_Isend(data,size,MPI_BYTE,dest,message,MPI_COMM_WORLD,rqst);
    else {
        MPI_Request mpi_request;
        MPI_Isend(data,size,MPI_BYTE,dest,message,MPI_COMM_WORLD,&mpi_request);
    }
}
void PROCESSOR::Recv(int dest,int message) {
    MPI_Recv(MPI_BOTTOM,0,MPI_INT,dest,message,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
}
void PROCESSOR::Recv(int dest,int message,void* data,int size) {
    MPI_Recv(data,size,MPI_BYTE,dest,message,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
}
void PROCESSOR::Wait(MPI_Request* rqst) {
    MPI_Wait(rqst,MPI_STATUS_IGNORE);
}
void PROCESSOR::Barrier() {
    MPI_Barrier(MPI_COMM_WORLD);
}
void PROCESSOR::Sum(float* sendbuf,float* recvbuf,int count) {
    MPI_Reduce(sendbuf,recvbuf,count,MPI_FLOAT,MPI_SUM,0,MPI_COMM_WORLD);
}
void PROCESSOR::Max(int* sendbuf,int* recvbuf,int count) {
    MPI_Reduce(sendbuf,recvbuf,count,MPI_INT,MPI_MAX,0,MPI_COMM_WORLD);
}
void PROCESSOR::Gather(char* sendbuf,char* recvbuf,int scount,int rcount) {
    MPI_Gather(sendbuf,scount,MPI_CHAR,recvbuf,rcount,MPI_CHAR,0,MPI_COMM_WORLD);
}
bool PROCESSOR::IProbe(int& source,int& message_id) {
    static MPI_Status mpi_status;
    int flag;
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,&flag,&mpi_status);
    if(flag) {
        message_id = mpi_status.MPI_TAG;
        source = mpi_status.MPI_SOURCE;
#ifdef MYDEBUG
        printf("<%012u> from [%d] to [%d] message \"%-6s\" \n",
            get_time(),source,host_id,message_str[message_id]);
#endif
        return true;
    }
    return false;
}
void PROCESSOR::send_pv(int dest, const PV_MESSAGE& pv) {
    MPI_Request rq;
    ISend(dest,PV,(void*)&pv,PV_MESSAGE_SIZE(pv),&rq);
    Wait(&rq);
}
void PROCESSOR::send_string(const char* str) {
    MPI_Request rq;
    ISend(0,STRING,(void*)&str[0],strlen(str)+1,&rq);
    Wait(&rq);
}
void PROCESSOR::send_init(int dest, const INIT_MESSAGE& init) {
    MPI_Request rq;
    ISend(dest,INIT,(void*)&init,INIT_MESSAGE_SIZE(init),&rq);
    Wait(&rq);
}
void PROCESSOR::send_cmd(const char* str) {
    for(int i = 0; i < PROCESSOR::n_hosts; i++) {
        if(i != PROCESSOR::host_id) {
            MPI_Request rq;
            ISend(i,STRING_CMD,(void*)&str[0],strlen(str)+1,&rq);
            Wait(&rq);
        }
    }
}
/**
* Handle messages
*/
void PROCESSOR::handle_message(int source,int message_id) {
    const PSEARCHER psb = processors[0]->searcher;

    /**************************************************
    * SPLIT  - Search from recieved position
    **************************************************/
    if(message_id == SPLIT) {
        SPLIT_MESSAGE split;
        Recv(source,message_id,&split,sizeof(SPLIT_MESSAGE));
        message_available = 0;

        /*setup board by undoing old moves and making new ones*/
        int i,score,move,using_pvs;
        if(split.pv_length) {
            for(i = 0;i < split.pv_length && i < psb->ply;i++) {
                if(split.pv[i] != psb->hstack[psb->hply - psb->ply + i].move) 
                    break;
            }
            while(psb->ply > i) {
                if(psb->hstack[psb->hply - 1].move) psb->POP_MOVE();
                else psb->POP_NULL();
            }
            for(;i < split.pv_length;i++) {
                if(split.pv[i]) psb->PUSH_MOVE(split.pv[i]);
                else psb->PUSH_NULL();
            }
        } else {
            psb->PUSH_MOVE(split.pv[0]);
        }
        /*reset*/
        SEARCHER::abort_search = 0;
        psb->clear_block();

        /**************************************
        * PVS-search on root node
        *************************************/
        processors[0]->state = GO;

        using_pvs = false;
        psb->pstack->extension = split.extension;
        psb->pstack->reduction = split.reduction;
        psb->pstack->depth = split.depth;
        psb->pstack->alpha = split.alpha;
        psb->pstack->beta = split.beta;
        psb->pstack->node_type = split.node_type;
        psb->pstack->search_state = split.search_state;
        if(psb->pstack->beta != psb->pstack->alpha + 1) {
            psb->pstack->node_type = CUT_NODE;
            psb->pstack->beta = psb->pstack->alpha + 1;
            using_pvs = true;
        }

        /*Search move and re-search if necessary*/
        move = psb->hstack[psb->hply - 1].move;
        while(true) {           
            psb->search();
            if(psb->stop_searcher || SEARCHER::abort_search) {
                move = 0;
                score = 0;
                break;
            }
            score = -psb->pstack->best_score;
            /*research with full depth*/
            if(psb->pstack->reduction
                && score >= -split.alpha
                ) {
                    psb->pstack->depth += psb->pstack->reduction;
                    psb->pstack->reduction = 0;

                    psb->pstack->alpha = split.alpha;
                    psb->pstack->beta = split.alpha + 1;
                    psb->pstack->node_type = CUT_NODE;
                    psb->pstack->search_state = NULL_MOVE;
                    continue;
            }
            /*research with full window*/
            if(using_pvs 
                && score > -split.beta
                && score < -split.alpha
                ) {
                    using_pvs = false;

                    psb->pstack->alpha = split.alpha;
                    psb->pstack->beta = split.beta;
                    psb->pstack->node_type = split.node_type;
                    psb->pstack->search_state = NULL_MOVE;
                    continue;
            }
            break;
        }

        /*undomove : Go to previous ply even if search was interrupted*/
        while(psb->ply > psb->stop_ply - 1) {
            if(psb->hstack[psb->hply - 1].move) psb->POP_MOVE();
            else psb->POP_NULL();
        }

        PROCESSOR::wait(0);

        /***********************************************************
        * Send result back and release all helper nodes we aquired
        ***********************************************************/
        PROCESSOR::cancel_idle_hosts();

        MERGE_MESSAGE merge;
        merge.nodes = psb->nodes;
        merge.qnodes = psb->qnodes;
        merge.ecalls = psb->ecalls;
        merge.nnecalls = psb->nnecalls;
        merge.time_check = psb->time_check;
        merge.splits = psb->splits;
        merge.bad_splits = psb->bad_splits;
        merge.egbb_probes = psb->egbb_probes;

        /*pv*/
        merge.master = split.master;
        merge.best_move = move;
        merge.best_score = score;
        merge.pv_length = 0;

        if(move && score > -split.beta && score < -split.alpha) {
            merge.pv[0] = move;
            memcpy(&merge.pv[1],&(psb->pstack + 1)->pv[psb->ply + 1],
                ((psb->pstack + 1)->pv_length - psb->ply ) * sizeof(MOVE));
            merge.pv_length = (psb->pstack + 1)->pv_length - psb->ply;
        }
        /*send it*/
        MPI_Request rq;
        ISend(source,PROCESSOR::MERGE,&merge,MERGE_MESSAGE_SIZE(merge),&rq);
        Wait(&rq);
    } else if(message_id == MERGE) {
        /**************************************************
        * MERGE  - Merge result of move at split point
        **************************************************/
        MERGE_MESSAGE merge;
        Recv(source,message_id,&merge,sizeof(MERGE_MESSAGE));
        

        /*update master*/
        PSEARCHER master = (PSEARCHER)merge.master;
        l_lock(master->lock);

        if(merge.best_move && merge.best_score > master->pstack->best_score) {
            master->pstack->best_score = merge.best_score;
            master->pstack->best_move = merge.best_move;
            if(merge.best_score > master->pstack->alpha) {
                if(merge.best_score > master->pstack->beta) {
                    master->pstack->flag = LOWER;

                    l_unlock(master->lock);
                    master->handle_fail_high();
                    l_lock(master->lock);
                } else {
                    master->pstack->flag = EXACT;
                    master->pstack->alpha = merge.best_score;

                    memcpy(&master->pstack->pv[master->ply],&merge.pv[0],
                            merge.pv_length * sizeof(MOVE));
                    master->pstack->pv_length = merge.pv_length + master->ply;
                }
            }
        }

        /*update counts*/
        master->nodes += merge.nodes;
        master->qnodes += merge.qnodes;
        master->ecalls += merge.ecalls;
        master->nnecalls += merge.nnecalls;
        master->time_check += merge.time_check;
        master->splits += merge.splits;
        master->bad_splits += merge.bad_splits;
        master->egbb_probes += merge.egbb_probes;

        l_unlock(master->lock);
        /* 
        * We finished searching one move from the current split. 
        * Check for more moves there and keep on searching.
        * Otherwise remove the node from the split's helper list, 
        * and add it to the list of idle helpers.
        */
        l_lock(lock_smp);
        SPLIT_MESSAGE& split = global_split[source];
        if(!master->stop_searcher && master->get_cluster_move(&split,true)) {
            l_unlock(lock_smp);
            ISend(source,PROCESSOR::SPLIT,&split,RESPLIT_MESSAGE_SIZE(split));
        } else {
            if(n_hosts > 2)
                ISend(source,CANCEL);
            else
                available_host_workers.push_back(source);
            l_unlock(lock_smp);
            /*remove from current split*/
            l_lock(master->lock);
            master->host_workers.remove(source);
            master->n_host_workers--;
            l_unlock(master->lock);
        }
        /******************************************************************
        * INIT  - Set up poistion from FEN and prepare threaded search
        ******************************************************************/
    } else if(message_id == INIT) {
        INIT_MESSAGE init;
        Recv(source,message_id,&init,sizeof(INIT_MESSAGE));
        
        /*setup board*/
        psb->set_board((char*)init.fen);

        /*make moves*/
        for(int i = 0;i < init.pv_length;i++) {
            if(init.pv[i]) psb->do_move(init.pv[i]);    
            else psb->do_null();
        }

        /*zero best move from all hosts*/
        for(int i = 0;i < PROCESSOR::n_hosts;i++) {
            PROCESSOR::node_pvs[i].pv_length = 0;
            PROCESSOR::node_pvs[i].pv[0] = 0;
        }

        /*wakeup main processor*/
        PROCESSOR::wait(0);

        /***********************************
        * Best move from slave hosts
        ************************************/
    } else if(message_id == PV) {;
        PV_MESSAGE pv;
        Recv(source,message_id, &pv, sizeof(PV_MESSAGE));
        l_lock(lock_smp);
        PROCESSOR::node_pvs[source] = pv;
        l_unlock(lock_smp);

        /***********************************
        * Any string ,such as PV,from slaves
        ************************************/
    } else if(message_id == STRING) {
        char str[1024];
        Recv(source,message_id,str,1024);

        /*print it to screen*/
        print_std(str);
        /***********************************
        * Commands to be executed
        ************************************/
    } else if(message_id == STRING_CMD) {
        char str[1024];
        Recv(source,message_id,str,1024);

        /*execute commands*/
        char*  commands[MAX_STR];
        commands[tokenize(str,commands)] = NULL;
        parse_commands(commands);

        /***********************************
        * Distributed transposition table
        ************************************/
#if CLUSTER_TT_TYPE == 1
    } else if(message_id == RECORD_TT) {
        TT_MESSAGE ttmsg;
        Recv(source,message_id,&ttmsg,sizeof(TT_MESSAGE));
        
        /*record*/
        psb->record_hash(ttmsg.col,ttmsg.hash_key,ttmsg.depth,ttmsg.ply,
                    ttmsg.flags,ttmsg.score,ttmsg.move,
                    ttmsg.mate_threat,ttmsg.singular);
    } else if(message_id == PROBE_TT) {
        TT_MESSAGE ttmsg;
        Recv(source,message_id,&ttmsg,sizeof(TT_MESSAGE));
        
        /*probe*/
        int proc_id = ttmsg.flags;
        int h_depth,score,mate_threat,singular;
        ttmsg.flags = psb->probe_hash(ttmsg.col,ttmsg.hash_key,ttmsg.depth,ttmsg.ply,
                    score,ttmsg.move,ttmsg.alpha,ttmsg.beta,
                    mate_threat,singular,h_depth,false);
        ttmsg.depth = h_depth;
        ttmsg.score = (int16_t)score;
        ttmsg.mate_threat = (uint8_t)mate_threat;
        ttmsg.singular = (uint8_t)singular;
        ttmsg.ply = proc_id;  //embed processor_id in message
        /*send*/
        MPI_Request rq;
        ISend(source,PROCESSOR::PROBE_TT_RESULT,&ttmsg,sizeof(TT_MESSAGE),&rq);
        Wait(&rq);
    } else if(message_id == PROBE_TT_RESULT) {
        TT_MESSAGE ttmsg;
        Recv(source,message_id,&ttmsg,sizeof(TT_MESSAGE));
        
        /*copy tt entry to processor*/
        int proc_id = ttmsg.ply;
        PPROCESSOR proc = processors[proc_id];
        proc->ttmsg = ttmsg;
        proc->ttmsg_recieved = true;
#endif
        /******************************************
        * Handle notification (zero-size) messages
        *******************************************/
    } else {
        Recv(source,message_id);
        
        if(message_id == HELP) {
            l_lock(lock_smp);
            if(n_idle_processors == n_processors)
                ISend(source,CANCEL);
            else
                available_host_workers.push_back(source);
            l_unlock(lock_smp);
        } else if(message_id == CANCEL) {
            help_messages--;
        } else if(message_id == QUIT) {
            SEARCHER::abort_search = 1;
        } else if(message_id == GOROOT) {
            message_available = 0;
            SEARCHER::chess_clock.infinite_mode = true;

            processors[0]->state = GO;

            l_lock(lock_smp);
            PROCESSOR::n_idle_processors--;
            PROCESSOR::set_num_searchers();
            l_unlock(lock_smp);

            psb->find_best();

            l_lock(lock_smp);
            PROCESSOR::n_idle_processors++;
            PROCESSOR::set_num_searchers();
            l_unlock(lock_smp);

            processors[0]->state = PARK;
        } else if(message_id == PONG) {
            pongs++;
        } else if(message_id == PING) {
            ISend(source,PONG);
        }
    }
}
/**
* Offer help to a randomly picked host
*/
void PROCESSOR::offer_help() {
    if(!help_messages
        && n_idle_processors == n_processors 
        && !use_abdada_cluster
        ) {
            int i, count = 0,dest;

            l_lock(lock_smp);
            for(i = 0;i < n_processors;i++) {
                if(processors[i]->state == WAIT) 
                    count++;
            }
            l_unlock(lock_smp);
            
            if(count == n_processors) {
                while(true) {
                    dest = (rand() % n_hosts);
                    if((dest != host_id) && (dest != prev_dest)) 
                        break;
                }
                ISend(dest,HELP);
                help_messages++;
                prev_dest = dest;
            }
    }
}
/**
* idle loop for message processing thread
*/
void PROCESSOR::message_idle_loop() {
    int message_id,source;
    while(true) {
        while(IProbe(source,message_id)) {
            g_message_id = message_id;
            g_source_id = source;
            /*Message thread handles all messages except SPLIT and GOROOT*/
            if(message_id == SPLIT || message_id == GOROOT) {
                message_available = 1;
                while(message_available) 
                    t_yield();
            } else {
                handle_message(source,message_id);
            }
        }
        offer_help();
        if(message_thread_state == PARK) {
            t_sleep(30);
        } else if(message_thread_state == KILL) {
            break;
        } else {
            t_yield();
            if(SEARCHER::use_nn) 
                t_sleep(SEARCHER::delay);
        }
    }
}
#endif
/**
* idle loop for all other threads
*/
void PROCESSOR::idle_loop() {
    bool skip_message = ((this != processors[0]) || (n_hosts == 1));
    do {
        if(state == PARK) {
            parked = 1;
            wait();
            parked = 0;
        } else if(state == WAIT) {
            t_yield();
            if(SEARCHER::use_nn) 
                t_sleep(SEARCHER::delay);
        }
        /*check message*/
        if(!skip_message) {
#ifdef CLUSTER
            int message_id,source;
            if(message_available) {
                message_id = g_message_id;
                source = g_source_id;
                handle_message(source,message_id);
            }
#endif
        }
        /*end*/
    } while(state <= WAIT);
}
/**
* Record hashtable entry in distributed system
*/
void SEARCHER::RECORD_HASH(
                 int col,const HASHKEY& hash_key,int depth,int ply,int flags,
                 int eval,int score,MOVE move,int mate_threat,int singular
                 ) {
#ifdef CLUSTER
#   if CLUSTER_TT_TYPE == 1
    bool local = (depth <= PROCESSOR::CLUSTER_SPLIT_DEPTH);
    int dest;
    if(!local) {
        dest = ((hash_key >> 48) * PROCESSOR::n_hosts) >> 16;
        if(dest == PROCESSOR::host_id) local = true;
    }
    if(!local) {
        /*construct message*/
        TT_MESSAGE ttmsg;
        ttmsg.hash_key = hash_key;
        ttmsg.score = score;
        ttmsg.depth = depth;
        ttmsg.flags = flags;
        ttmsg.ply = ply;
        ttmsg.col = col;
        ttmsg.mate_threat = mate_threat;
        ttmsg.singular = singular;
        ttmsg.move = move;
        /*send it*/
        MPI_Request rq;
        PROCESSOR::ISend(dest,PROCESSOR::RECORD_TT,&ttmsg,sizeof(TT_MESSAGE),&rq);
        PROCESSOR::Wait(&rq);
    } else 
#   endif
#endif
    {
        record_hash(col,hash_key,depth,ply,flags,eval,score,move,mate_threat,singular);
    }
}
/**
* Read hashtable entry in distributed system
*/
int SEARCHER::PROBE_HASH(
               int col,const HASHKEY& hash_key,int depth,int ply,int& eval, int& score,
               MOVE& move,int alpha,int beta,int& mate_threat,int& singular,int& h_depth,
               bool exclusiveP
               ) {
#ifdef CLUSTER
#   if CLUSTER_TT_TYPE == 1
    bool local = (depth <= PROCESSOR::CLUSTER_SPLIT_DEPTH);
    int dest = 0;
    if(!local) {
        dest = ((hash_key >> 48) * PROCESSOR::n_hosts) >> 16;
        if(dest == PROCESSOR::host_id) local = true;
    }
    if(!local) {
        PPROCESSOR proc = processors[processor_id];
        /*construct message*/
        TT_MESSAGE& ttmsg = proc->ttmsg;
        ttmsg.hash_key = hash_key;
        ttmsg.score = score;
        ttmsg.depth = depth;
        ttmsg.flags = processor_id; //embed processor_id in message
        ttmsg.ply = ply;
        ttmsg.col = col;
        ttmsg.mate_threat = mate_threat;
        ttmsg.singular = singular;
        ttmsg.alpha = alpha;
        ttmsg.beta = beta;
        ttmsg.move = move;
        /*send it*/
        proc->ttmsg_recieved = false;
        PROCESSOR::ISend(dest,PROCESSOR::PROBE_TT,&ttmsg,sizeof(TT_MESSAGE));
        /*wait*/ 
        while(!proc->ttmsg_recieved && !abort_search) {
            proc->idle_loop();
            t_yield();
        }
        /*return*/
        h_depth = ttmsg.depth;
        score = ttmsg.score;
        mate_threat = ttmsg.mate_threat;
        singular = ttmsg.singular;
        return ttmsg.flags;
    } else
#   endif
#endif
    {
        return probe_hash(col,hash_key,depth,ply,eval,score,move,alpha,beta,
                mate_threat,singular,h_depth,exclusiveP);
    }
}
/**
exit scorpio 
*/
void PROCESSOR::exit_scorpio(int status) {
    for(int  i = 1; i < PROCESSOR::n_processors; i++) {
        PROCESSOR::wait(i);
        PROCESSOR::kill(i);
    }
    CLUSTER_CODE(message_thread_state = KILL;)

    if(!log_on)
        remove_log_file();

    for(int  i = 1; i < PROCESSOR::n_processors; i++)
        threads[i].join();
    CLUSTER_CODE(if(message_thread.joinable()) message_thread.join();)

#ifdef CLUSTER
    print("Process [%d/%d] terminated.\n",host_id,n_hosts);
    if(PROCESSOR::n_hosts > 1)
        MPI_Abort(MPI_COMM_WORLD,0);
    else
        MPI_Finalize();
#else
    print("Process terminated.\n");
#endif
    exit(EXIT_SUCCESS);
}
#ifdef CLUSTER
/**
* Get move for host helper
*/
int SEARCHER::get_cluster_move(SPLIT_MESSAGE* split, bool resplit) {

    l_lock(lock);
TOP:
    if(!get_move()) {
        l_unlock(lock);
        return false;
    }

    pstack->legal_moves++;

    /*play the move*/
    PUSH_MOVE(pstack->current_move);

    /*set next ply's depth and be selective*/           
    pstack->depth = (pstack - 1)->depth - 1;
    if(be_selective((pstack - 1)->legal_moves, false)) {
        POP_MOVE();
        goto TOP;
    }
    /*fill in split info*/
    split->master = (int64_t)this;
    split->alpha = -(pstack - 1)->beta;
    split->beta = -(pstack - 1)->alpha;
    split->depth = pstack->depth;
    split->node_type = (pstack - 1)->next_node_type;
    split->search_state = NULL_MOVE;
    split->extension = pstack->extension;
    split->reduction = pstack->reduction;
    if(resplit) {
        split->pv_length = 0;
        split->pv[0] = (pstack - 1)->current_move;
    } else {
        split->pv_length = ply;
        for(int i = 0;i < ply;i++)
            split->pv[i] = hstack[hply - ply + i].move;
    }
    /*undo move*/
    POP_MOVE();

    l_unlock(lock);
    return true;
}
/**
* Cancel idle hosts
*/
void PROCESSOR::cancel_idle_hosts() {
    l_lock(lock_smp);

    int dest;
    while(!available_host_workers.empty()) {
        dest = *(available_host_workers.begin());
        ISend(dest,CANCEL);
        available_host_workers.pop_front();
    }

    l_unlock(lock_smp);
}
/**
* Quit hosts 
*/
void PROCESSOR::quit_hosts() {
    for(int i = 0;i < n_hosts;i++) {
        if(i != host_id)
            ISend(i,QUIT);
    }
}
/**
* Wait hosts 
*/
void PROCESSOR::wait_hosts() {
    while(pongs < n_hosts - 1)
        t_sleep(1);
    pongs = 0;
}
/**
* Get initial position
*/
void SEARCHER::get_init_pos(INIT_MESSAGE* init) {
    int i,len,move;

    /*undo to last fifty move*/       
    len = fifty + 1;
    for(i = 0;i < len && hply > 0;i++) {
        if(hstack[hply - 1].hash_key == 0) break;
        if(hstack[hply - 1].move) undo_move();
        else undo_null();
    }
    get_fen((char*)init->fen);

    /*redo moves*/
    len = i;
    init->pv_length = 0;
    for(i = 0;i < len;i++) {
        move = hstack[hply].move;
        init->pv[init->pv_length++] = move;
        if(move) do_move(move);
        else do_null();
    }

}
#endif

/**
* Update bounds,score,move
*/
#define UPDATE_BOUND(ps1,ps2) {           \
    ps1->best_score = ps2->best_score;    \
    ps1->best_move  = ps2->best_move;     \
    ps1->flag       = ps2->flag;          \
    ps1->alpha      = ps2->alpha;         \
    ps1->beta       = ps2->beta;          \
}

/**
* Get SMP split move
*/
int SEARCHER::get_smp_move() {

    l_lock(master->lock);
    if(!master->get_move()) {
        l_unlock(master->lock);
        return false;
    }

    /*update  counts*/
    pstack->count = master->pstack->count;
    pstack->current_index = master->pstack->current_index;
    pstack->current_move = master->pstack->current_move;
    pstack->move_st[pstack->current_index - 1] = 
        master->pstack->move_st[pstack->current_index - 1];
    pstack->score_st[pstack->current_index - 1] = 
        master->pstack->score_st[pstack->current_index - 1];
    pstack->gen_status = master->pstack->gen_status;
    pstack->noncap_start = master->pstack->noncap_start;
    pstack->legal_moves = ++master->pstack->legal_moves;

    /*synchronize bounds*/
    if(pstack->best_score > master->pstack->best_score) {
        UPDATE_BOUND(master->pstack,pstack);
    } else if(pstack->best_score < master->pstack->best_score) {
        UPDATE_BOUND(pstack,master->pstack);
        pstack->best_move = 0;
    }

    l_unlock(master->lock);
    return true;
}

/**
* Copy local search result of this thread back to the master. 
* We have been updating search bounds whenever we got a new move.
*/
void SEARCHER::update_master(int skip) {
    /*update counts*/
    master->nodes += nodes;
    master->qnodes += qnodes;
    master->ecalls += ecalls;
    master->nnecalls += nnecalls;
    master->time_check += time_check;
    master->splits += splits;
    master->bad_splits += bad_splits;
    master->egbb_probes += egbb_probes;
    master->seldepth = MAX(master->seldepth, seldepth);

    if(!skip) {
        /*update stuff at split point. First FIX the stack location because 
        we may have reached here from an unfinished search where "stop_search" flag is on.
        */
        ply = master->ply;
        hply = master->hply;
        pstack = stack + ply;

        /*check if the master needs to be updated.
        We only do this for the sake of the last move played!*/
        if(pstack->best_score > master->pstack->best_score) {
            UPDATE_BOUND(master->pstack,pstack);
        }

        /*best move of local search matches with that of the master's*/
        if(pstack->best_move == master->pstack->best_move) {

            if(pstack->flag == EXACT || (!ply && pstack->flag == LOWER)) {
                memcpy(&master->pstack->pv[ply],&pstack->pv[ply],
                    (pstack->pv_length - ply ) * sizeof(MOVE));
                master->pstack->pv_length = pstack->pv_length;
            }

            for(int i = 0;i < MAX_PLY;i++) {
                master->stack[i].killer[0] = stack[i].killer[0];
                master->stack[i].killer[1] = stack[i].killer[1]; 
            }
        }
    }
    
    /*zero helper*/
    master->workers[processor_id] = 0;
    master->n_workers--;
}

/**
* Attach processor to help at the split node.
* Copy board and other relevant data..
*/
void SEARCHER::attach_processor(int new_proc_id) {
    int j = 0;
    for(j = 0; (j < MAX_SEARCHERS_PER_CPU) && processors[new_proc_id]->searchers[j].used; j++);
    if(j < MAX_SEARCHERS_PER_CPU) {
        PSEARCHER psearcher = &processors[new_proc_id]->searchers[j];
        psearcher->COPY(this);
        psearcher->clear_block();
        psearcher->master = this;
        psearcher->processor_id = new_proc_id;
        processors[new_proc_id]->searcher = psearcher;
        workers[new_proc_id] = psearcher;
        n_workers++;
    }
}
bool PROCESSOR::has_block() {
    for(int j = 0;j < MAX_SEARCHERS_PER_CPU; j++) {
        if(!searchers[j].used)
            return true;
    }
    return false;
}
/**
* clear searcher block
*/
void SEARCHER::clear_block() {
    master = 0;
    stop_ply = ply;
    stop_searcher = 0;
    finish_search = false;
    skip_nn = false;
    used = true;
    pstack->pv_length = ply;
    pstack->best_move = 0;
#ifdef CLUSTER
    n_host_workers = 0;
    host_workers.clear();
#endif
    n_workers = 0;
    for(int i = 0; i < PROCESSOR::n_processors;i++)
        workers[i] = 0;

    /*reset counts*/
    nodes = 0;
    qnodes = 0;
    ecalls = 0;
    nnecalls = 0;
    time_check = 0;
    splits = 0;
    bad_splits = 0;
    egbb_probes = 0;
    seldepth = 0;
}
/**
* Stop workers at split point
*/
void SEARCHER::stop_workers() {
    l_lock(lock);
    for(int i = 0; i < PROCESSOR::n_processors; i++) {
        SEARCHER* pworker = workers[i].load();
        if(pworker) {
            if(pworker->n_workers) 
                pworker->stop_workers();
            pworker->stop_searcher = 1;
        }
    }
#ifdef CLUSTER
    if(n_host_workers) {
        std::list<int>::iterator it;
        for(it = host_workers.begin();it != host_workers.end();++it)
            PROCESSOR::ISend(*it,PROCESSOR::QUIT);
    }
#endif
    l_unlock(lock);
}

/**
* Fail high handler
*/
void SEARCHER::handle_fail_high() {
    /*only once*/
    l_lock(lock);
    if(stop_searcher) {
        l_unlock(lock);
        return;
    }
    stop_searcher = 1;
    l_unlock(lock);

    bad_splits++;
    /*stop workers*/
    l_lock(lock_smp);
    stop_workers();
    l_unlock(lock_smp);
}

/**
* Check if splitting tree is possible after at least one move is searched (YBW concept).
* We look for both idle hosts and idle threads to share the work.
*/
int SEARCHER::check_split() {
    int i;
    if(((pstack->depth > PROCESSOR::SMP_SPLIT_DEPTH && PROCESSOR::n_idle_processors > 0)
        CLUSTER_CODE( || 
        (pstack->depth > PROCESSOR::CLUSTER_SPLIT_DEPTH && PROCESSOR::available_host_workers.size() > 0)))
        && !stop_searcher
        && ((stop_ply != ply) || (!master))
        && pstack->gen_status < GEN_END
        && processors[processor_id]->has_block()
        ) {
            
            l_lock(lock_smp);
            
#ifdef CLUSTER
            /*attach helper hosts*/
            if(pstack->depth > PROCESSOR::CLUSTER_SPLIT_DEPTH 
                && !use_abdada_cluster
                && PROCESSOR::available_host_workers.size() > 0
                ) {
                    while(n_host_workers < MAX_CPUS_PER_SPLIT && !PROCESSOR::available_host_workers.empty()) {
                        int dest = *(PROCESSOR::available_host_workers.begin());

                        SPLIT_MESSAGE& split = global_split[dest];
                        if(!get_cluster_move(&split))
                            break;

                        l_lock(lock);
                        n_host_workers++;
                        host_workers.push_back(dest);
                        l_unlock(lock);

                        PROCESSOR::ISend(dest,PROCESSOR::SPLIT,&split,SPLIT_MESSAGE_SIZE(split));

                        PROCESSOR::available_host_workers.pop_front();
                    }
            }
#endif
            
            /*attach helper threads*/
            if(pstack->depth > PROCESSOR::SMP_SPLIT_DEPTH 
                && PROCESSOR::n_idle_processors > 0
                ) {
                    for(i = 0;i < PROCESSOR::n_processors && i < PROCESSOR::n_cores && n_workers < MAX_CPUS_PER_SPLIT - 1;i++) {
                        if(processors[i]->state == WAIT) {
                            attach_processor(i);
                            /*if depth greater than cluster_split_depth, attach only one thread*/
                            CLUSTER_CODE(
                                if(!use_abdada_cluster && pstack->depth > PROCESSOR::CLUSTER_SPLIT_DEPTH) break;
                            )
                        }
                    }
            }
    
            /*send them off to work*/
            if(n_workers CLUSTER_CODE(|| n_host_workers)) {
                splits++;
                attach_processor(processor_id);
                for(i = 0; i < PROCESSOR::n_processors; i++) {
                    if(workers[i])
                        processors[i]->state = GO;
                }
                l_unlock(lock_smp);
                return true;
            }
            /*end*/

            l_unlock(lock_smp);
    }
    return false;
}

/**
* Create/kill search thread
*/
static void reset_tables(PPROCESSOR proc, int tid) {
    if(tid < PROCESSOR::n_cores || !(montecarlo && SEARCHER::use_nn)) {
        if(!(montecarlo && frac_abprior == 0)) {
            proc->reset_hash_tab(tid);
            proc->reset_eval_hash_tab();
        }
        if(!SEARCHER::use_nnue) {
            proc->reset_pawn_hash_tab();
        }
    }
}
static void CDECL thread_proc(int id) {
    long tid = id;
    PPROCESSOR proc = new PROCESSOR();
    proc->searcher = NULL;
    proc->state = PARK;
    processors[tid] = proc;
    reset_tables(proc,tid);
    proc->clear_tables(id);
    search((PPROCESSOR)proc);
}
void PROCESSOR::create(int id) {
    threads[id] = t_create(thread_proc,id);
}
void PROCESSOR::kill(int id) {
    PPROCESSOR proc = processors[id];
    proc->state = KILL;
    while(proc->state == KILL) 
        t_yield();
    proc->delete_tables();
    delete proc;
    processors[id] = 0;
}
void PROCESSOR::park(int id) {
    PPROCESSOR proc = processors[id];
    proc->state = PARK;
    while(!proc->parked)
        t_yield();
}
void PROCESSOR::wait(int id) {
    PPROCESSOR proc = processors[id];
    proc->state = WAIT;
    proc->signal();
}

/**
* Initialize mt number of threads by creating/deleting 
* threads from the pool of processors.
*/
void init_smp(int mt) {
    PPROCESSOR proc = processors[0];
    int n_procs = PROCESSOR::n_processors;

    PROCESSOR::n_processors = mt;
    reset_tables(proc,0);

    if(n_procs < mt) {
        for(int i = 1; i < MAX_CPUS;i++) {
            if(n_procs < mt) {
                if(processors[i] == 0) {
                    PROCESSOR::create(i);
                    n_procs++;
                }
            } 
        }
        while(PROCESSOR::n_idle_processors < mt - 1)
            t_yield();
    } else if(n_procs > mt) {
        for(int i = MAX_CPUS - 1; i >= 1;i--) {
            if(n_procs > mt) {
                if(processors[i] != 0) {
                    PROCESSOR::kill(i);
                    n_procs--;
                }
            }
        }
    }
}

/**
* Set main search thread
*/
void PROCESSOR::set_main() {
    PPROCESSOR proc = new PROCESSOR();
    proc->searcher = &proc->searchers[0];
    proc->searcher->used = true;
    proc->searcher->processor_id = 0;
    proc->state = GO;
    processors[0] = proc;
}
