#include "scorpio.h"

/*
* Distributed search.
*    Scorpio uses a decentralized approach (p2p) where neither memory nor
*    jobs are centrialized. Each host could have multiple processors in which case
*    shared memory search (centralized search with threads) will be used.
*    One processor per node will be started by mpirun, then each process 
*    at each node will create enough threads to engage all its processors.
*/

#ifdef CLUSTER
static SPLIT_MESSAGE global_split[MAX_HOSTS];
static MERGE_MESSAGE global_merge;
static INIT_MESSAGE  global_init;

MPI_Datatype INIT_Datatype;
void init_messages() {
	MPI_Datatype init_otype[] = {MPI_CHAR,MPI_INT};
	int init_count[] = {MAX_FEN_STR,128};
	MPI_Aint adr1,adr2;
	MPI_Address(&global_init.fen,&adr1);
	MPI_Address(&global_init.pv_length,&adr2);
	MPI_Aint init_offset[] = {0,adr2 - adr1};
	MPI_Type_struct(2,init_count,init_offset,init_otype,&INIT_Datatype);
	MPI_Type_commit(&INIT_Datatype);
}
#endif

/*
* IDLE loop of processes and threads
*/
#if defined(PARALLEL) || defined(CLUSTER)

void PROCESSOR::idle_loop() {

#ifndef CLUSTER
	while(state == WAIT);
#else

	/*
	* Only processor 0 checks the messages.
	* This makes the message polling code non-critical section
	* and also simplifies it a lot. However we still use lock_mpi 
	* because other thread could invoke MPI calls during splits or fail highs.
	*/
	if(this != processors || n_hosts == 1) {
		while(state == WAIT);
		return;
	}
	/*
	* Process messages if we have one, otherwise send one "HELP"
	* message if this host is idle and wait for a "CANCEL" or "SPLIT"
	*/
	int flag;
	do {
		/* 
		* Polling. MPI_Iprobe<->MPI_Recv is not thread safe.
		*/
		l_lock(lock_mpi);
		MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,&flag,&mpi_status);

		/*Message recieved?*/
		if(flag) {
			const PSEARCHER psb = processors[0].searcher;
			int message_id = mpi_status.MPI_TAG;
			int source = mpi_status.MPI_SOURCE;
#ifdef _DEBUG
			print("\"%-6s\" message from [%d] to [%d] {thread %d} at <"FMT64">\n",
				message_str[message_id],source,host_id,this - processors,get_time());
#endif
			if(message_id == HELP) {
				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);

				/*add the host to our list of helpers*/
				l_lock(lock_smp);
				if(n_idle_processors == n_processors) {
					l_unlock(lock_smp);
					Non_Blocking_Send(source,CANCEL);
				} else {
					available_host_workers.push_back(source);
					l_unlock(lock_smp);
				}
				/*end*/
			} else if(message_id == CANCEL) {
				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);

				help_messages--;
			} else if(message_id == SPLIT) {
				SPLIT_MESSAGE& split = global_split[host_id];
				MPI_Recv(&split,sizeof(SPLIT_MESSAGE),MPI_BYTE,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);

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
				psb->master = 0;
				psb->stop_ply = psb->ply;
				psb->stop_searcher = 0;
				psb->used = true;
				psb->n_host_workers = 0;
				psb->host_workers.clear();
				psb->n_workers = 0;
				for(i = 0; i < PROCESSOR::n_processors;i++)
					psb->workers[i] = 0;

				/*reset counts*/
				psb->nodes = 0;
				psb->qnodes = 0;
				psb->time_check = 0;
				psb->full_evals = 0;
				psb->lazy_evals = 0;
				psb->splits = 0;
				psb->bad_splits = 0;
				psb->egbb_probes = 0;

				/*
				 * Search
				 */
				processors[0].state = GO;

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

REDO:				
				/*call search now*/
				move = psb->hstack[psb->hply - 1].move;

				psb->search();

				if(psb->stop_searcher || SEARCHER::abort_search)
					move = 0;

				score = -psb->pstack->best_score;

				if(move) {
					/*research with full window*/
					if(using_pvs && score > -split.beta
						&& score < -split.alpha
						) {
							using_pvs = false;
							psb->pstack->alpha = split.alpha;
							psb->pstack->beta = split.beta;
							psb->pstack->node_type = split.node_type;
							psb->pstack->search_state = NULL_MOVE;
							goto REDO;
					}
					/*research with full depth*/
					if(psb->pstack->reduction
						&& score >= -split.alpha
						) {
							psb->pstack->depth += psb->pstack->reduction;
							psb->pstack->reduction = 0;

							psb->pstack->alpha = split.alpha;
							psb->pstack->beta = split.beta;
							psb->pstack->node_type = split.node_type;
							psb->pstack->search_state = NULL_MOVE;

							goto REDO;
					}
				}

				/*undomove : Go to previous ply even if search was interrupted*/
				while(psb->ply > psb->stop_ply - 1) {
					if(psb->hstack[psb->hply - 1].move) psb->POP_MOVE();
					else psb->POP_NULL();
				}

				processors[0].state = WAIT;

				/*
				 * Cancel all unused helper hosts
				 */
				PROCESSOR::cancel_idle_hosts();
				/*
				 * Send back result
				 */
				MERGE_MESSAGE& merge = global_merge;
				merge.nodes = psb->nodes;
				merge.qnodes = psb->qnodes;
				merge.time_check = psb->time_check;
				merge.full_evals = psb->full_evals;
				merge.lazy_evals = psb->lazy_evals;
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
				l_lock(lock_mpi);
				MPI_Isend(&merge,MERGE_MESSAGE_SIZE(merge),MPI_BYTE,source,MERGE,MPI_COMM_WORLD,&mpi_request);
				l_unlock(lock_mpi);

			} else if(message_id == MERGE) {
				MERGE_MESSAGE& merge = global_merge;
				MPI_Recv(&merge,sizeof(MERGE_MESSAGE),MPI_BYTE,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);

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
				master->full_evals += merge.full_evals;
				master->lazy_evals += merge.lazy_evals;
				master->splits += merge.splits;
				master->bad_splits += merge.bad_splits;
				master->egbb_probes += merge.egbb_probes;

				l_unlock(master->lock);

				/* Now that result is fully backed up to master, add the
				* host to the list of available hosts. MERGE acts like an implicit HELP offer.
				*/
				SPLIT_MESSAGE& split = global_split[source];
				if(master->get_cluster_move(&split)) {
					l_lock(lock_mpi);
					MPI_Isend(&split,SPLIT_MESSAGE_SIZE(split),MPI_BYTE,source,PROCESSOR::SPLIT,MPI_COMM_WORLD,&mpi_request);
					l_unlock(lock_mpi);
				} else {
					/*remove host*/
					l_lock(master->lock);
					master->host_workers.remove(source);
					master->n_host_workers--;
					l_unlock(master->lock);

					l_lock(lock_smp);
					available_host_workers.push_back(source);
					l_unlock(lock_smp);
				}
				/*end*/

			} else if(message_id == INIT) {
				INIT_MESSAGE& init = global_init;
				MPI_Recv(&init,1,INIT_Datatype,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);

				/*setup board*/
				psb->set_board(init.fen);
				/*make moves*/
				register int i;
				for(i = 0;i < init.pv_length;i++) {
					if(init.pv[i]) psb->do_move(init.pv[i]);	
					else psb->do_null();
				}
#ifdef PARALLEL
				/*wakeup processors*/
				for(i = 1;i < n_processors;i++)
					processors[i].state = WAIT;
				while(n_idle_processors != n_processors)
					t_sleep(1);
#endif
			} else if(message_id == QUIT) {
				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);

				SEARCHER::abort_search = 1;
			} else if(message_id == RELAX) {
				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);
#ifdef PARALLEL
				for(int i = 1;i < n_processors;i++)
					processors[i].state = PARK;
#endif
			} else if(message_id == ABORT) {
				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);

				PROCESSOR::exit_scorpio(EXIT_SUCCESS);
			} else if(message_id == PING) {
				MPI_Recv(MPI_BOTTOM,0,MPI_INT,source,message_id,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
				l_unlock(lock_mpi);

				Non_Blocking_Send(source,PONG);
			}
		} else {
			l_unlock(lock_mpi);

			/* Check if this host is idle and 
			* send "HELP" message to a randomly picked host
			*/
			if(state == WAIT 
				&& n_idle_processors == n_processors 
				&& !help_messages 
				) {
					register int i, count = 0,dest;

					l_lock(lock_smp);
					for(i = 0;i < n_processors;i++) {
						if(processors[i].state == WAIT) 
							count++;
					}
					if(count == n_processors) {
						while((dest = (rand() % n_hosts)) == host_id); 
						Non_Blocking_Send(dest,HELP);
						help_messages++;
					}
					l_unlock(lock_smp);
			}
		}
	} while(state == WAIT || flag);
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
		Non_Blocking_Send(dest,CANCEL);
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
			Non_Blocking_Send(i,QUIT);
	}
}
/*
* Abort hosts 
*/
void PROCESSOR::abort_hosts() {
	for(int i = 0;i < n_hosts;i++) {
		if(i != host_id)
			Non_Blocking_Send(i,ABORT);
		print("Process [%d/%d] terminated.\n",i,n_hosts);
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
void CDECL thread_proc(void* processor) {
	search((PPROCESSOR)processor);
}

void PROCESSOR::create() {
	pthread_t thread = 0;
	state = CREATE;
	searcher = NULL;
	t_create(thread_proc,this,thread);
	reset_eval_hash_tab();
	reset_pawn_hash_tab();
	while(state == CREATE);
}

void PROCESSOR::kill() {
	searcher = NULL;
	delete[] eval_hash_tab;
	delete[] pawn_hash_tab;
	state = DEAD;
}
/*
* Attach processor to help at the split node.
* Copy board and other relevant data..
*/
void SEARCHER::attach_processor(int new_proc_id) {

	register int i,j = 0;
	for(j = 0; j < MAX_SEARCHERS && searchers[j].used; j++);
	if(j < MAX_SEARCHERS) {

		PSEARCHER psearcher = &searchers[j];
		psearcher->COPY(this);
		psearcher->master = this;
		psearcher->stop_searcher = 0;
		psearcher->processor_id = new_proc_id;
		psearcher->stop_ply = ply;
		psearcher->pstack->pv_length = ply;
		psearcher->pstack->best_move = 0;
		psearcher->used = true;
#ifdef CLUSTER
		psearcher->n_host_workers = 0;
		psearcher->host_workers.clear();
#endif
		psearcher->n_workers = 0;
		for(i = 0; i < PROCESSOR::n_processors;i++)
			psearcher->workers[i] = 0;

		/*reset counts*/
		psearcher->nodes = 0;
		psearcher->qnodes = 0;
		psearcher->time_check = 0;
		psearcher->full_evals = 0;
		psearcher->lazy_evals = 0;
		psearcher->splits = 0;
		psearcher->bad_splits = 0;
		psearcher->egbb_probes = 0;
		/*end*/

		processors[new_proc_id].searcher = psearcher;
		workers[new_proc_id] = psearcher;
		n_workers++;
	}
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
	master->full_evals += full_evals;
	master->lazy_evals += lazy_evals;
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

		if(pstack->flag == EXACT) {
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
		CLUSTER_CODE( || (DEPTH(pstack->depth) > PROCESSOR::CLUSTER_SPLIT_DEPTH && PROCESSOR::available_host_workers.size() > 0)))
		&& !stop_searcher
		&& stop_ply != ply
		&& pstack->gen_status < GEN_END
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

							l_lock(lock_mpi);
							MPI_Isend(&split,SPLIT_MESSAGE_SIZE(split),MPI_BYTE,dest,PROCESSOR::SPLIT,
								MPI_COMM_WORLD,&PROCESSOR::mpi_request);
							l_unlock(lock_mpi);

							PROCESSOR::available_host_workers.pop_front();
					}
			}
#endif
			/*attach threads*/
			if(DEPTH(pstack->depth) > PROCESSOR::SMP_SPLIT_DEPTH 
				&& PROCESSOR::n_idle_processors > 0
				) {
					for(i = 0;i < PROCESSOR::n_processors && n_workers < MAX_CPUS_PER_SPLIT;i++) {
						if(processors[i].state == WAIT)
							attach_processor(i);
					}
			}

			/*send them off to work*/
			if(n_workers CLUSTER_CODE(|| n_host_workers)) {
				splits++;
#if defined(_DEBUG) && defined(CLUSTER)
				print("[%d : %d] Split %d at [%d %d]\n",PROCESSOR::host_id,
					processor_id,splits,ply,pstack->depth);
#endif
				attach_processor(processor_id);
				for(i = 0; i < PROCESSOR::n_processors; i++) {
					if(workers[i]) {
						processors[i].state = GO;
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
			Non_Blocking_Send(*it,PROCESSOR::QUIT);
	}
#endif
	l_unlock(lock);
}

#endif
/*
* Fail high handler
*/
#if defined(PARALLEL) || defined(CLUSTER)
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
				if(processors[i].state == DEAD) {
					processors[i].create();
					PROCESSOR::n_processors++;
					print("+ Thread %d started.\n",i);
				}
			} 
		}
	} else if(PROCESSOR::n_processors > mt) {
		for(i = MAX_CPUS - 1; i >= 1;i--) {
			if(PROCESSOR::n_processors > mt) {
				if(processors[i].state != DEAD) {
					processors[i].kill();
					PROCESSOR::n_processors--;
					print("- Thread %d terminated.\n",i);
				}
			}
		}
	}
}
#endif
