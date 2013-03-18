#include "scorpio.h"

/*
* Distributed search.
*    Scorpio uses a decentralized approach (p2p) where neither memory nor
*    jobs are centrialized. Each host could have multiple processors in which case
*    shared memory search (centralized search with threads) will be used.
*    One process per node will be started by mpirun, then each process 
*    at each node will create enough threads to engage all its processors.
*/

#ifdef CLUSTER

static SPLIT_MESSAGE global_split[MAX_HOSTS];
static MERGE_MESSAGE global_merge;
static INIT_MESSAGE  global_init;
static VOLATILE int g_message_id;
static VOLATILE int g_source_id;
static MPI_Status mpi_status;
static MPI_Request mpi_request;

/*
Message polling thread for cluster
*/
static void CDECL check_messages(void*) {
	PROCESSOR::message_idle_loop();
}
/*
* Initialize MPI
*/
void PROCESSOR::init(int argc, char* argv[]) {
	int namelen,provided,requested = MPI_THREAD_SINGLE;
	MPI_Init_thread(&argc, &argv,requested,&provided);
	MPI_Comm_size(MPI_COMM_WORLD, &PROCESSOR::n_hosts);
	MPI_Comm_rank(MPI_COMM_WORLD, &PROCESSOR::host_id);
	MPI_Get_processor_name(PROCESSOR::host_name, &namelen);
	print("Process [%d/%d] on %s : pid %d\n",PROCESSOR::host_id,
		PROCESSOR::n_hosts,PROCESSOR::host_name,GETPID());
#ifdef THREAD_POLLING
	if(n_hosts > 1)
		t_create(check_messages,0);
#endif
}
/*
* MPI calls
*/
void PROCESSOR::ISend(int dest,int message) {
	MPI_Isend(MPI_BOTTOM,0,MPI_INT,dest,message,MPI_COMM_WORLD,&mpi_request);
}
void PROCESSOR::ISend(int dest,int message,void* data,int size) {
	MPI_Isend(data,size,MPI_BYTE,dest,message,MPI_COMM_WORLD,&mpi_request);
}
void PROCESSOR::Recv(int dest,int message) {
	MPI_Recv(MPI_BOTTOM,0,MPI_INT,dest,message,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
}
void PROCESSOR::Recv(int dest,int message,void* data,int size) {
	MPI_Recv(data,size,MPI_BYTE,dest,message,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
}
bool PROCESSOR::IProbe(int& source,int& message_id) {
	int flag;
	MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,&flag,&mpi_status);
	if(flag) {
		message_id = mpi_status.MPI_TAG;
		source = mpi_status.MPI_SOURCE;
#ifdef _DEBUG
		print("\"%-6s\" message from [%d] to [%d] at <"FMT64">\n",
			message_str[message_id],source,host_id,get_time());
#endif
		return true;
	}
	return false;
}
/*
* Handle messages
*/
void PROCESSOR::handle_message(int source,int message_id) {
	const PSEARCHER psb = processors[0]->searcher;

	/**************************************************
	* SPLIT  - Search from recieved position
	**************************************************/
	if(message_id == SPLIT) {
		SPLIT_MESSAGE& split = global_split[host_id];
		Recv(source,message_id,&split,sizeof(SPLIT_MESSAGE));
		message_available = 0;

		/*setup board by undoing old moves and making new ones*/
		register int i,score,move,using_pvs;
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

		processors[0]->state = WAIT;

		/***********************************************************
		* Send result back and release all helper nodes we aquired
		***********************************************************/
		PROCESSOR::cancel_idle_hosts();

		MERGE_MESSAGE& merge = global_merge;
		merge.nodes = psb->nodes;
		merge.qnodes = psb->qnodes;
		merge.time_check = psb->time_check;
		merge.splits = psb->splits;
		merge.bad_splits = psb->bad_splits;
		merge.egbb_probes = psb->egbb_probes;

		/*pv*/
		merge.master = split.master;
		merge.best_move = move;
		merge.best_score = score;
		merge.pv_length = psb->ply;

		if(move && score > -split.beta && score < -split.alpha) {
			merge.pv[psb->ply] = move;
			memcpy(&merge.pv[psb->ply + 1],&(psb->pstack + 1)->pv[psb->ply + 1],
				((psb->pstack + 1)->pv_length - psb->ply ) * sizeof(MOVE));
			merge.pv_length = (psb->pstack + 1)->pv_length;
		}
		/*send it*/
		ISend(source,PROCESSOR::MERGE,&merge,MERGE_MESSAGE_SIZE(merge));
	} else if(message_id == MERGE) {
		/**************************************************
		* MERGE  - Merge result of move at split point
		**************************************************/
		MERGE_MESSAGE& merge = global_merge;
		Recv(source,message_id,&merge,sizeof(MERGE_MESSAGE));
		message_available = 0;

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

					memcpy(&master->pstack->pv[master->ply],&merge.pv[master->ply],
						(merge.pv_length - master->ply ) * sizeof(MOVE));
					master->pstack->pv_length = merge.pv_length;
				}
			}
		}

		/*update counts*/
		master->nodes += merge.nodes;
		master->qnodes += merge.qnodes;
		master->time_check += merge.time_check;
		master->splits += merge.splits;
		master->bad_splits += merge.bad_splits;
		master->egbb_probes += merge.egbb_probes;

		l_unlock(master->lock);
		/* 
		* We finished searching one move from the current split. Check for more moves there and keep on searching.
		* Otherwise remove the node from the split's helper list, and add it to the list of idle helpers.
		*/
		l_lock(lock_smp);
		SPLIT_MESSAGE& split = global_split[source];
		if(!master->stop_searcher && master->get_cluster_move(&split)) {
			ISend(source,PROCESSOR::SPLIT,&split,SPLIT_MESSAGE_SIZE(split));
			l_unlock(lock_smp);
		} else {
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
		INIT_MESSAGE& init = global_init;
		Recv(source,message_id,&init,sizeof(INIT_MESSAGE));
		message_available = 0;
		/*setup board*/
		psb->set_board((char*)init.fen);

		/*make moves*/
		register int i;
		for(i = 0;i < init.pv_length;i++) {
			if(init.pv[i]) psb->do_move(init.pv[i]);	
			else psb->do_null();
		}
#ifdef PARALLEL
		/*wakeup processors*/
		for(i = 0;i < n_processors;i++)
			processors[i]->state = WAIT;
#endif
		/***********************************
		* Handle notification messages
		************************************/
	} else {
		Recv(source,message_id);
		message_available = 0;
		if(message_id == HELP) {
			l_lock(lock_smp);
			if(n_idle_processors == n_processors) {
				l_unlock(lock_smp);
				ISend(source,CANCEL);
			} else {
				available_host_workers.push_back(source);
				l_unlock(lock_smp);
			}
		} else if(message_id == CANCEL) {
			help_messages--;
		} else if(message_id == QUIT) {
			SEARCHER::abort_search = 1;
#ifdef PARALLEL
		} else if(message_id == RELAX) {
			for(int i = 0;i < n_processors;i++)
				processors[i]->state = PARK;
#endif
		} else if(message_id == ABORT) {
			PROCESSOR::exit_scorpio(EXIT_SUCCESS);
		} else if(message_id == PING) {
			ISend(source,PONG);
		}
	}
}
/*
* Offer help to a randomly picked host
*/
void PROCESSOR::offer_help() {
	if(!help_messages
		&& n_idle_processors == n_processors 
		) {
			register int i, count = 0,dest;

			l_lock(lock_smp);
			for(i = 0;i < n_processors;i++) {
				if(processors[i]->state == WAIT) 
					count++;
			}
			if(count == n_processors) {
				while((dest = (rand() % n_hosts)) == host_id); 
				ISend(dest,HELP);
				help_messages++;
			}
			l_unlock(lock_smp);
	}
}
/*
* idle loop for message processing thread
*/
void PROCESSOR::message_idle_loop() {
	int message_id,source;
	while(true) {
		while(IProbe(source,message_id)) {
			g_message_id = message_id;
			g_source_id = source;
			if(message_id == SPLIT) {
				message_available = 1;
				while(message_available) 
					t_yield();
			} else {
				handle_message(source,message_id);
			}
		}
		offer_help();
		t_yield();
	}
}

#endif
/*
* idle loop for all threads
*/
#if defined(PARALLEL) || defined(CLUSTER)
void PROCESSOR::idle_loop() {
	if((this != processors[0]) || (n_hosts == 1)) {
		while(state <= WAIT) {
			if(state == PARK) t_sleep(1);
		}
		return;
	}
#ifdef CLUSTER
	int message_id,source;
#ifdef THREAD_POLLING
	while(state <= WAIT) {
		if(state == PARK) t_sleep(1);
		if(!message_available)
			continue;
		message_id = g_message_id;
		source = g_source_id;
		handle_message(source,message_id);
	}
#else
	do {
		while(IProbe(source,message_id))
			handle_message(source,message_id);
		offer_help();
		if(state == PARK) t_sleep(1);
	} while(state <= WAIT);
#endif
#endif
}
#endif

#ifdef CLUSTER
/*
* Get move for host helper
*/
int SEARCHER::get_cluster_move(SPLIT_MESSAGE* split) {

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
	pstack->depth = (pstack - 1)->depth - UNITDEPTH;
	if(be_selective()) {
		POP_MOVE();
		goto TOP;
	}
	/*fill in split info*/
	split->master = (BMP64)this;
	split->alpha = -(pstack - 1)->beta;
	split->beta = -(pstack - 1)->alpha;
	split->depth = pstack->depth;
	split->node_type = (pstack - 1)->next_node_type;
	split->search_state = NULL_MOVE;
	split->extension = pstack->extension;
	split->reduction = pstack->reduction;
	split->pv_length = ply;
	for(int i = 0;i < ply;i++)
		split->pv[i] = hstack[hply - ply + i].move;

	/*undo move*/
	POP_MOVE();

	l_unlock(lock);
	return true;
}
/*
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
/*
* Quit hosts 
*/
void PROCESSOR::quit_hosts() {
	for(int i = 0;i < n_hosts;i++) {
		if(i != host_id)
			ISend(i,QUIT);
	}
}
/*
* Abort hosts 
*/
void PROCESSOR::abort_hosts() {
	for(int i = 0;i < n_hosts;i++) {
		if(i != host_id)
			ISend(i,ABORT);
		print("Process [%d/%d] terminated.\n",i,n_hosts);
	}
}
/*
* Get initial position
*/
void SEARCHER::get_init_pos(INIT_MESSAGE* init) {
	int i,len,move;

	/*undo to last fifty move*/       
	len = fifty + 1;
	for(i = 0;i < len && hply > 0;i++) {
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

#ifdef PARALLEL

/*
* update bounds,score,move
*/
#define UPDATE_BOUND(ps1,ps2) {           \
	ps1->best_score = ps2->best_score;    \
	ps1->best_move  = ps2->best_move;     \
	ps1->flag       = ps2->flag;          \
	ps1->alpha      = ps2->alpha;         \
	ps1->beta       = ps2->beta;          \
}

/*
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
/*
* Create/kill search thread
*/
static volatile bool t_started;

void CDECL thread_proc(void* id) {
	long tid = (long)id;
	PPROCESSOR proc = new PROCESSOR();
	proc->searcher = NULL;
	proc->state = PARK;
	proc->reset_hash_tab(tid,0);
	proc->reset_eval_hash_tab();
	proc->reset_pawn_hash_tab();
	processors[tid] = proc;
	t_started = true;
	search((PPROCESSOR)proc);
}
void PROCESSOR::set_main() {
	PPROCESSOR proc = new PROCESSOR();
	proc->searcher = &proc->searchers[0];
	proc->searcher->used = true;
	proc->searcher->processor_id = 0;
	proc->state = GO;
	proc->reset_hash_tab(0,0);
	proc->reset_eval_hash_tab();
	proc->reset_pawn_hash_tab();
	processors[0] = proc;
}
void PROCESSOR::create(int id) {
	long tid = id;
	t_started = false;
	t_create(thread_proc,tid);
	while(t_started == false);
}
void PROCESSOR::kill(int id) {
	PPROCESSOR proc = processors[id];
	proc->state = KILL;
	while(proc->state == KILL);
	proc->delete_hash_tables();
	delete proc;
	processors[id] = 0;
}
/*
* Attach processor to help at the split node.
* Copy board and other relevant data..
*/
void SEARCHER::attach_processor(int new_proc_id) {
	register int j = 0;
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
/*
* Copy local search result of this thread back to the master. 
* We have been updating search bounds whenever we got a new move.
*/
void SEARCHER::update_master() {

	/*update counts*/
	master->nodes += nodes;
	master->qnodes += qnodes;
	master->time_check += time_check;
	master->splits += splits;
	master->bad_splits += bad_splits;
	master->egbb_probes += egbb_probes;

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

	/*zero helper*/
	master->workers[processor_id] = 0;
	master->n_workers--;
}

/*
* Check if splitting tree is possible after at least one move is searched (YBW concept).
* We look for both idle hosts and idle threads to share the work.
*/
int SEARCHER::check_split() {
	register int i;
	if(((DEPTH(pstack->depth) > PROCESSOR::SMP_SPLIT_DEPTH && PROCESSOR::n_idle_processors > 0)
		CLUSTER_CODE( || 
		(DEPTH(pstack->depth) > PROCESSOR::CLUSTER_SPLIT_DEPTH && PROCESSOR::available_host_workers.size() > 0)))
		&& !stop_searcher
		&& ((stop_ply != ply) || (!master))
		&& pstack->gen_status < GEN_END
		&& processors[processor_id]->has_block()
		) {
			
			l_lock(lock_smp);
			
#ifdef CLUSTER
			/*attach helper hosts*/
			if(DEPTH(pstack->depth) > PROCESSOR::CLUSTER_SPLIT_DEPTH 
				&& PROCESSOR::available_host_workers.size() > 0
				) {
					int dest;
					while(n_host_workers < MAX_CPUS_PER_SPLIT && !PROCESSOR::available_host_workers.empty()) {
							dest = *(PROCESSOR::available_host_workers.begin());

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
			/*attach threads*/
			if(DEPTH(pstack->depth) > PROCESSOR::SMP_SPLIT_DEPTH 
				&& PROCESSOR::n_idle_processors > 0
				) {
					for(i = 0;i < PROCESSOR::n_processors && n_workers < MAX_CPUS_PER_SPLIT;i++) {
						if(processors[i]->state == WAIT)
							attach_processor(i);
					}
			}

			/*send them off to work*/
			if(n_workers CLUSTER_CODE(|| n_host_workers)) {
				splits++;
				attach_processor(processor_id);
				for(i = 0; i < PROCESSOR::n_processors; i++) {
					if(workers[i]) {
						processors[i]->state = GO;
					}
				}
				l_unlock(lock_smp);
				return true;
			}
			/*end*/

			l_unlock(lock_smp);
	}
	return false;
}
/*
* Stop workers at split point
*/
void SEARCHER::stop_workers() {
	l_lock(lock);
	for(int i = 0; i < PROCESSOR::n_processors; i++) {
		if(workers[i]) {
			if(workers[i]->n_workers) 
				workers[i]->stop_workers();
			workers[i]->stop_searcher = 1;
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

#endif

#if defined(PARALLEL) || defined(CLUSTER)
/*
* clear searcher block
*/
void SEARCHER::clear_block() {
	master = 0;
	stop_ply = ply;
	stop_searcher = 0;
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
	time_check = 0;
	splits = 0;
	bad_splits = 0;
	egbb_probes = 0;
}
/*
* Fail high handler
*/
void SEARCHER::handle_fail_high() {
	l_lock(lock_smp);
	if(stop_searcher) {
		l_unlock(lock_smp);
		return;
	}
	stop_workers();
	l_unlock(lock_smp);

	stop_searcher = 1;
	bad_splits++;
}
/*
* Initialize mt number of threads by creating/deleting 
* threads from the pool of processors.
*/
void init_smp(int mt) {
	register int i;

	if(PROCESSOR::n_processors < mt) {
		for(i = 1; i < MAX_CPUS;i++) {
			if(PROCESSOR::n_processors < mt) {
				if(processors[i] == 0) {
					PROCESSOR::create(i);
					PROCESSOR::n_processors++;
					print("+ Thread %d started.\n",i);
				}
			} 
		}
	} else if(PROCESSOR::n_processors > mt) {
		for(i = MAX_CPUS - 1; i >= 1;i--) {
			if(PROCESSOR::n_processors > mt) {
				if(processors[i] != 0) {
					PROCESSOR::kill(i);
					PROCESSOR::n_processors--;
					print("- Thread %d terminated.\n",i);
				}
			}
		}
	}
}
#endif
