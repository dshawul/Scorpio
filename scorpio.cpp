#include "scorpio.h"

#define VERSION __TIME__

/*
all external variables declared here
*/
const int pawn_dir[2] = {
	UU,DD
};
const int pic_tab[14] = {
	blank,king,queen,rook,bishop,knight,pawn,
	king,queen,rook,bishop,knight,pawn,elephant
};
const int piece_cv[14] = {
	0,0,9,5,3,3,1,0,9,5,3,3,1,0
};
const int piece_see_v[14] = {
	0,9900,900,500,300,300,100,9900,900,500,300,300,100,0
};
const int piece_mask[14] = {
	0,KM,QM,RM,BM,NM,WPM,KM,QM,RM,BM,NM,BPM,0
};

int pcsq[14][0x80];
bool book_loaded = false;
bool log_on = false;
int scorpio_start_time;

/*
parallel search
*/
PPROCESSOR processors[MAX_CPUS] = {0};
int PROCESSOR::n_processors;
VOLATILE int PROCESSOR::n_idle_processors;
int PROCESSOR::n_hosts = 1;

#ifdef PARALLEL
LOCK  lock_smp;
LOCK  lock_io;
int PROCESSOR::SMP_SPLIT_DEPTH = 4;
#endif

#ifdef CLUSTER
VOLATILE int PROCESSOR::message_available = 0;
int PROCESSOR::MESSAGE_POLL_NODES = 200;
int PROCESSOR::CLUSTER_SPLIT_DEPTH = 8;
int PROCESSOR::host_id;
char PROCESSOR::host_name[256];
list<int> PROCESSOR::available_host_workers;
int PROCESSOR::help_messages = 0;
const char *const PROCESSOR::message_str[] = {
	"QUIT","INIT","RELAX","HELP","CANCEL","SPLIT","MERGE","PING","PONG","ABORT"
};
#endif
/*
static global variables of SEARCHER
*/
UBMP64 SEARCHER::root_score_st[MAX_MOVES];
unsigned int SEARCHER::history[14][64];
CHESS_CLOCK SEARCHER::chess_clock;
int SEARCHER::search_depth;
int SEARCHER::start_time;
int SEARCHER::scorpio;
int SEARCHER::pv_print_style;
int SEARCHER::root_score;
int SEARCHER::root_failed_low;
int SEARCHER::last_book_move;
int SEARCHER::first_search;
int SEARCHER::probe_depth;
int SEARCHER::analysis_mode = false;
int SEARCHER::in_egbb;
int SEARCHER::show_full_pv;
int SEARCHER::abort_search;
unsigned int SEARCHER::poll_nodes;
MOVE SEARCHER::expected_move;
int SEARCHER::resign_value;
int SEARCHER::resign_count;
char HIST_STACK::start_fen[MAX_FEN_STR];
/*
static global variables/functions
*/
static SEARCHER searcher;
static PSEARCHER main_searcher;
static int ponder = false;
static int result = R_UNKNOWN;
static char egbb_path[MAX_STR] = "./egbb/";
static int  egbb_cache_size = 4;

static bool load_ini();
static void init_game();

/*
load egbbs with another thread
*/
static VOLATILE bool egbb_thread_stoped = false;
static void CDECL egbb_thread_proc(void*) {
	int start = get_time();
	egbb_cache_size = (egbb_cache_size * 1024 * 1024);
	SEARCHER::egbb_is_loaded = LoadEgbbLibrary(egbb_path,egbb_cache_size);
	int end = get_time();
	print("loading_time = %ds\n",(end - start) / 1000);
	egbb_thread_stoped = true;
}
/*
exit scorpio 
*/
void PROCESSOR::exit_scorpio(int status) {
	CLUSTER_CODE(MPI_Finalize());
	exit(status);
}
/*
only winboard protocol support
*/
int CDECL main(int argc, char* argv[]) {
	char   buffer[MAX_FILE_STR];
	char*  commands[MAX_STR];

	/*init game*/
	init_game();

	/*
	 * Parse scorpio.ini file which contains settings of scorpio. 
	 * Search/eval execution commands should not be put there.
	 */
	if(!load_ini()) 
		return 0;
	/*
	 * Start loading egbbs with a separate thread.
	 * If there are command line options wait for the egbb to load fully.  
	 */
	t_create(egbb_thread_proc,0);
	if(argc)
		while(!egbb_thread_stoped) t_sleep(100);
	/*
	 * Initialize MPI
	 */
#ifdef CLUSTER
	PROCESSOR::init(argc,argv);
	if(PROCESSOR::host_id == 0) {
#endif
		/*
		 * Parse commands from the command line
		 */
		strcpy(buffer,"");
		for(int i = 1;i < argc;i++)	{
			strcat(buffer," ");
			strcat(buffer,argv[i]);
		}
		print_log("<COMMAND LINE>%s\n",buffer);

		commands[tokenize(buffer,commands)] = NULL;
		if(!parse_commands(commands))
			goto END;

		/* 
		 * If log=off in ini and log=off from command line, delete log file.
		 * If log=off in ini and log=on  from command line, only rank=0 will have log file.
		 * If log=on  in ini,then each processor will have separate log files.
		 */
		if(!log_on)
			remove_log_file();

		/*
		 * Parse commands from stdin.
		 */
		print_log("==============================\n");
		while(true) {
			if(!read_line(buffer))
				goto END;
			commands[tokenize(buffer,commands)] = NULL;
			if(!parse_commands(commands))
				goto END;
		}
#ifdef  CLUSTER
	} else {
		/* Delete the log file.*/
		if(!log_on)
			remove_log_file();
		/* goto wait mode */
		processors[0]->state = PARK;
		search(processors[0]);
	}
#endif

END:
	PROCESSOR::exit_scorpio(EXIT_SUCCESS);
}

/*
initialize game
*/
void init_game() {
#ifdef PARALLEL
	l_create(lock_smp);
	l_create(lock_io);
#endif
	init_io();
	print("feature done=0\n");
	scorpio_start_time = get_time();
	PROCESSOR::n_idle_processors = 0;
	PROCESSOR::n_processors = 1;
	PROCESSOR::set_main();
	main_searcher = processors[0]->searcher;
	SEARCHER::egbb_is_loaded = false;
	initmagicmoves();
	searcher.pre_calculate();
	searcher.new_board();
	SEARCHER::scorpio = black;
	SEARCHER::pv_print_style = 0;
	SEARCHER::resign_value = 600;
	SEARCHER::resign_count = 0;
#ifdef BOOK_PROBE
	load_book();
#endif
}
/*
"help" command added by Dann Corbit
*/
static const char *const commands_recognized[] = {
	". -- Send a search status update while analyzing.",
	"? -- Move now.",
	"accepted -- Response to the feature command.",
	"analyze -- Enter analyze mode.",
	"build <pgn> <book> <size> <half_plies> <color>"
	"\n\t<pgn> source file with pgn games"
	"\n\t<book> destination book file"
	"\n\t<size> Number of book entries to be used during book build"
	"\n\t<half_plies> Number of half plies considered"
	"\n\t<color> white/black postions",
	"book <on/off> -- Turns book usage on/off.",
	"computer -- Inform Scorpio that the opponent is also a computer chess engine.",
	"d -- Debugging command to print the current board.",
	"easy -- Turn off pondering.",
	"egbb_cache_size -- Set the egbb (endgame bitbase) cache size in megabytes.",
	"egbb_load_type -- Set the egbb load type:\n\t 0 = none\n\t 1 = all 3/4 men\n"
	"\t 2 = selective loading of 5 men (not implemented yet)\n\t 3 = all 5 men",
	"egbb_path -- Set the egbb path.",
	"eht -- Set evaluation hash table size in megabytes.",
	"exit -- Leave analysis mode.",
	"force -- Set the engine to play neither color ('force mode'). Stop clocks.",
	"go -- Leave force mode and set the engine to play the color that is on move.",
	"hard -- Turn on pondering (thinking on the opponent's time or permanent brain).",
	"help -- Produce this listing of supported commands.",
	"history -- Debugging command to print game history.",
	"ht -- Set hash table size in megabytes.",
	"level -- level <MPS> <BASE> <INC> {Set time controls}.",
	"log -- turn loggin on/onff.",
	"merge <book1> <book2> <book> <w1> <w2> \n\tMerge two books with weights <w1> and <w2> and save restult in book.",
	"mirror -- Debugging command to mirror the current board.",
	"moves -- Debugging command to print all possible moves for the current board.",
	"mt/cores   <N>      -- Set the number of parallel threads of execution to N.",
	"          auto      -- Set to available logical cores.",
	"          auto-<R>  -- Set to (auto - R) logical cores.",
	"name -- Tell scorpio who you are.",
	"new -- Reset the board to the standard chess starting position.",
	"nopost -- do not show thinking.",
	"otim -- otim N {Set a clock that belongs to the opponent in centiseconds.}",
	"perft -- perft <depth> performs a move generation count to <depth>.",
	"pht -- Set pawn hash table size in megabytes.",
	"post -- Show thinking.",
	"protover -- protover <N> {Command sent immediately after the 'xboard' command.}",
	"pvstyle -- syle for pv output.",
	"quit -- The chess engine should immediately exit.",
	"random -- This GnuChess command is ignored.",
	"rejected -- Response to the feature command.",
	"remove -- The user asks to back up full move.",
	"resign <RESIGN_VALUE> -- Sets resign value.",
	"result -- Sets the game result.",
	"runeval -- runeval <epdfile> runs the evalution function on EPD file <epdfile>.",
	"runsearch -- runsearch <epdfile> runs the engine search on EPD file <epdfile>.",
	"score -- score runs the evaluation function on the current position.",
	"sd -- sd <DEPTH> {The engine should limit its thinking to <DEPTH> ply.}",
	"setboard -- setboard <FEN> is used to set up FEN position <FEN>.",
	"st -- st <TIME> {Set time controls search time in seconds.}",
	"time -- time <N> {Set a clock that belongs to the engine in centiseconds.}",
	"undo -- The user asks to back up one half move.",
	"xboard -- Request xboard mode (the default).",
	NULL
};
/*
parse_commands
*/
bool parse_commands(char** commands) {

	char*  command;
	int    command_num;
	MOVE   move;
	char   mv_str[10];
	int    do_search;
	HASHKEY hash_in = searcher.hash_key;

	command_num = 0;

	while((command = commands[command_num++]) != 0) {
		/*
		xboard
		*/
		do_search = false;

		if (!strcmp(command, "protover")) {
			print("feature name=1 myname=\"Scorpio_%s\"\n",VERSION);
			print("feature sigint=0 sigterm=0\n");
			print("feature setboard=1 draw=0 colors=0\n");
			print("feature smp=0 memory=0 egt=\"scorpio\"\n");
			print("feature option=\"log -check %d\"\n",log_on);
			print("feature option=\"clear_hash -button\"\n");
			print("feature option=\"resign -spin %d 100 30000\"\n",SEARCHER::resign_value);
			print_search_params();
#ifdef TUNE
			print_eval_params();
#endif
			while(!egbb_thread_stoped) t_sleep(100);
			print("feature done=1\n");

			command_num++;
		} else if (!strcmp(command, "xboard")) {
		} else if (!strcmp(command, "computer")
			|| !strcmp(command, "post")
			|| !strcmp(command, "nopost")
			|| !strcmp(command, "random")
			|| !strcmp(command, "option")
			) {
		} else if (!strcmp(command, "?")) {
			SEARCHER::abort_search = 1;
		} else if (!strcmp(command, ".")) {
			if(!SEARCHER::abort_search) {
				int time_used = get_time() - SEARCHER::start_time; 
				mov_str(main_searcher->stack[0].current_move,mv_str);
				print("stat01: %d "FMT64" %d %d %d %s\n",time_used / 10,main_searcher->nodes,
					main_searcher->search_depth,
					main_searcher->stack[0].count - main_searcher->stack[0].current_index - 1,
					main_searcher->stack[0].count,mv_str);
			}
		} else if (!strcmp(command, "accepted")
			|| !strcmp(command, "rejected")
			) {
				command_num++;
		} else if (!strcmp(command, "name")) {
			print("Hello ");
			while(true) {
				command = commands[command_num++];
				if(!command) break;
				print(command);
			}
			print("!\n");
		} else if(!strcmp(command,"st")) {
			SEARCHER::chess_clock.max_sd = MAX_PLY;
			SEARCHER::chess_clock.max_st = 1000 * (atoi(commands[command_num++]));
		} else if(!strcmp(command,"sd")) {
			SEARCHER::chess_clock.max_st = MAX_NUMBER;
			SEARCHER::chess_clock.max_sd = atoi(commands[command_num++]);
		} else if(!strcmp(command,"level")) {
			SEARCHER::chess_clock.mps = atoi(commands[command_num++]);
			if(strstr(commands[command_num],":")) {
				int mn,sec;
				sscanf(commands[command_num],"%d:%d",&mn,&sec);
				SEARCHER::chess_clock.p_time = 60000 * mn + 1000 * sec;
				command_num++;
			} else {
				SEARCHER::chess_clock.p_time = 60000 * atoi(commands[command_num++]);
			}
			SEARCHER::chess_clock.inc = 1000 * atoi(commands[command_num++]);
			SEARCHER::chess_clock.o_time = searcher.chess_clock.p_time;
			SEARCHER::chess_clock.max_st = MAX_NUMBER;
			SEARCHER::chess_clock.max_sd = MAX_PLY;
		} else if(!strcmp(command,"time")) {
			SEARCHER::chess_clock.p_time = (10 * atoi(commands[command_num++]));
		} else if(!strcmp(command,"otim")) {
			SEARCHER::chess_clock.o_time = (10 * atoi(commands[command_num++]));
		} else if(!strcmp(command,"hard")) {
			ponder = true;
		} else if(!strcmp(command,"easy")) {
			ponder = false;
		} else if(!strcmp(command,"force")) {
			SEARCHER::scorpio = neutral;
		} else if(!strcmp(command,"exit")) {
			SEARCHER::analysis_mode = false;
			if(SEARCHER::chess_clock.infinite_mode) 
				return false;
		} else if(!strcmp(command,"result")) {
			SEARCHER::abort_search = 1;
			if(!strcmp(commands[command_num],"1-0"))
				result = R_WWIN;
			else if(!strcmp(commands[command_num],"0-1"))
				result = R_BWIN;
			else if(!strcmp(commands[command_num],"1/2-1/2"))
				result = R_DRAW;
			else
				result = R_UNKNOWN;
			while(commands[++command_num]);
			searcher.print_game();
		} else if(!strcmp(command,"quit")) {
			CLUSTER_CODE(PROCESSOR::abort_hosts());
			print("Bye Bye\n");
			PROCESSOR::exit_scorpio(EXIT_SUCCESS);
		} else if (!strcmp(command, "clear_hash")) {
			PROCESSOR::clear_hash_tables();
		} else if (!strcmp(command, "new")) {
			PROCESSOR::clear_hash_tables();
			searcher.new_board();
			result = R_UNKNOWN;
			SEARCHER::scorpio = black;
		} else if(!strcmp(command,"setboard")) {
			PROCESSOR::clear_hash_tables();
			char fen[MAX_STR];
			strcpy(fen,commands[command_num++]);
			strcat(fen," ");
			strcat(fen,commands[command_num++]);
			strcat(fen," ");
			strcat(fen,commands[command_num++]);
			strcat(fen," ");
			strcat(fen,commands[command_num++]);
			strcat(fen," ");
			if(commands[command_num] && isdigit(commands[command_num][0])) {
				strcat(fen,commands[command_num++]);
				strcat(fen," ");

				if(commands[command_num] && isdigit(commands[command_num][0])) {
					strcat(fen,commands[command_num++]);
					strcat(fen," ");
				}
			}
			searcher.set_board(fen);
			result = R_UNKNOWN;
		} else if(!strcmp(command,"undo")) {
			if(searcher.hply >= 1) searcher.undo_move();
		} else if(!strcmp(command,"remove")) {
			if(searcher.hply >= 1) searcher.undo_move();
			if(searcher.hply >= 1) searcher.undo_move();
		} else if(!strcmp(command,"go")) {
			SEARCHER::scorpio = searcher.player;
			do_search = true;
		} else if(!strcmp(command,"analyze")) {
			SEARCHER::analysis_mode = true;
			do_search = true;
			/*
			hash tables
			*/
		} else if(!strcmp(command,"ht")) {
			UBMP32 size = 1,size_max = (atoi(commands[command_num++]) * 1024 * 1024) / (2 * sizeof(HASH));
			while(2 * size <= size_max) size *= 2;
			for(int i = 0;i < PROCESSOR::n_processors;i++) 
				processors[i]->reset_hash_tab(i,size);
			print("ht %d X %d = %.1f MB\n",2 * size,sizeof(HASH),(2 * size * sizeof(HASH)) / double(1024 * 1024));
		} else if(!strcmp(command,"pht")) {
			UBMP32 size = 1,size_max = (atoi(commands[command_num++]) * 1024 * 1024) / (sizeof(PAWNHASH));
			while(2 * size <= size_max) size *= 2;
			for(int i = 0;i < PROCESSOR::n_processors;i++) 
				processors[i]->reset_pawn_hash_tab(size);
			print("pht %d X %d = %.1f MB\n",size,sizeof(PAWNHASH),(size * sizeof(PAWNHASH)) / double(1024 * 1024));
		} else if(!strcmp(command,"eht")) {
			UBMP32 size = 1,size_max = (atoi(commands[command_num++]) * 1024 * 1024) / (sizeof(EVALHASH));
			while(2 * size <= size_max) size *= 2;
			for(int i = 0;i < PROCESSOR::n_processors;i++) 
				processors[i]->reset_eval_hash_tab(size);
			print("eht %d X %d = %.1f MB\n",size,sizeof(EVALHASH),(size * sizeof(EVALHASH)) / double(1024 * 1024));
			/*
			parallel search
			*/
		} else if(!strcmp(command,"mt") || !strcmp(command,"cores") ) {
#ifdef PARALLEL
			int mt;
			if(!strcmp(commands[command_num],"auto"))
				mt = get_number_of_cpus();
			else if(!strncmp(commands[command_num],"auto-",5)) {
				int r = atoi(&commands[command_num][5]);
				mt = get_number_of_cpus() - r;
			} else
				mt = atoi(commands[command_num]);
			mt = MIN(mt, MAX_CPUS);
			init_smp(mt);
			print("processors [%d]\n",PROCESSOR::n_processors);
#endif
			command_num++;
			/*
			egbb
			*/
#ifdef EGBB
		} else if(!strcmp(command, "egbb_path")) {
			strcpy(egbb_path,commands[command_num]);
			command_num++;
		} else if(!strcmp(command, "egtpath")) {
			command_num++;
			strcpy(egbb_path,commands[command_num]);
			command_num++;
		} else if(!strcmp(command, "egbb_cache_size")) {
			egbb_cache_size = atoi(commands[command_num]);
			command_num++;
		} else if(!strcmp(command, "egbb_load_type")) {
			SEARCHER::egbb_load_type = atoi(commands[command_num]);
			command_num++;
#endif
#ifdef BOOK_PROBE
		} else if (!strcmp(command, "book")) {
			if(commands[command_num]) {
				if(!strcmp(commands[command_num],"on"))
					book_loaded = true;
				else if(!strcmp(commands[command_num],"off"))
					book_loaded = false;
				command_num++;
			} else if(book_loaded) {
				searcher.show_book_moves();
			}
#endif
#ifdef BOOK_CREATE
		} else if (!strcmp(command,"build")) {
			int col = neutral,hsize = 1024 * 1024,plies = 30;
			char source[1024] = "book.pgn",dest[1024] = "book.dat";

			int k = 0;
			while(true) {
				command = commands[command_num++];
				if(!command) break;
				if(!strcmp(command,"white")) col = black;
				else if(!strcmp(command,"black")) col = white;
				else  {
					if(k == 0) strcpy(source,command);
					else if(k == 1) strcpy(dest,command);
					else if(k == 2) hsize = atoi(command);
					else if(k == 3) plies = atoi(command);
					k++;
				}
			}
			searcher.build_book(source,dest,hsize,plies,col);
		} else if (!strcmp(command,"merge")) {
			char source1[1024] = "book1.dat",source2[1024] = "book2.dat",dest[1024] = "book.dat";
			double w1 = 0,w2 = 0;
			int k = 0;
			while(true) {
				command = commands[command_num++];
				if(!command) break;
				if(k == 0) strcpy(source1,command);
				else if(k == 1) strcpy(source2,command);
				else if(k == 2) strcpy(dest,command);
				else if(k == 3) w1 = atof(command);
				else if(k == 4) w2 = atof(command);
				k++;
			}
			merge_books(source1,source2,dest,w1,w2);
#endif
#ifdef LOG_FILE
		} else if (!strcmp(command, "log")) {
			if(!strcmp(commands[command_num],"on") ||
				!strcmp(commands[command_num],"1"))
				log_on = true;
			else
				log_on = false;
			command_num++;
#endif
		} else if(check_search_params(commands,command,command_num)) {
#ifdef TUNE
		} else if(check_eval_params(commands,command,command_num)) {
#endif
		} else if(!strcmp(command, "resign")) {
			SEARCHER::resign_value = atoi(commands[command_num]);
			command_num++;
		} else if(!strcmp(command, "help")) {
			size_t index = 0;
			while (commands_recognized[index]) {
				puts(commands_recognized[index]);
				index++;
			}
			/*
			debugging
			*/
		} else if(!strcmp(command,"d")) {
			searcher.print_board();
		} else if(!strcmp(command,"mirror")) {
			searcher.mirror();
		} else if(!strcmp(command,"history")) {
			searcher.print_history();
		} else if(!strcmp(command,"moves")) {
			searcher.print_allmoves();
		} else if(!strcmp(command,"pvstyle")) {
			SEARCHER::pv_print_style = atoi(commands[command_num]);
			command_num++;
		} else if(!strcmp(command,"perft")) {
			clock_t start,end;
			int depth = atoi(commands[command_num++]);
			start = clock();
			UBMP64 nodes = searcher.perft(depth);
			end = clock();
			print("\nnodes "FMT64"\n",nodes);
			print("time %.2f sec\n",(end - start) / 1000.0f);
		} else if(!strcmp(command,"score")) {
			int score;
			if(searcher.all_man_c <= 5) {
				searcher.probe_bitbases(score);
				print("bitbase_score = %d\n",score);
			} else {
				score = searcher.eval();
				print("score = %d\n",score);
			}
		} else if (!strcmp(command, "runeval") || !strcmp(command, "runsearch") ) {

			char input[MAX_STR],fen[MAX_STR];
			char* words[100];
			int sc = 0,visited = 0;
			bool eval_test = !strcmp(command,"runeval");

			FILE *fd;

			fd = fopen(commands[command_num++],"r");

			if(!fd) {
				print("epd file not found!\n");
				continue;
			}

			if(SEARCHER::pv_print_style != 1) 
				print("******************************************\n");
			else
				print("\n\t\tNodes     Time      NPS      splits     bad"
				"\n\t\t=====     ====      ===      ======     ===\n");

			while(fgets(input,MAX_STR,fd)) {
				input[strlen(input) - 1] = 0;
				tokenize(input,words);
				strcpy(fen,words[0]);
				strcat(fen," ");
				strcat(fen,words[1]);
				strcat(fen," ");
				strcat(fen,words[2]);
				strcat(fen," ");
				strcat(fen,words[3]);
				strcat(fen," ");
				visited++;
				searcher.set_board(fen);

				if(eval_test) {
					sc = searcher.eval();
					searcher.mirror();
					int sc1 = searcher.eval();
					if(sc == sc1)
						print("*%d* %d\n",visited,sc);
					else {
						print("*****WRONG RESULT*****\n");
						print("[ %s ] \nsc = %6d sc1 = %6d\n",fen,sc,sc1);
						print("**********************\n");
					}
				} else {
					PROCESSOR::clear_hash_tables();
					main_searcher->COPY(&searcher);
					main_searcher->find_best();
					if(SEARCHER::pv_print_style != 1) 
						print("********** %d ************\n",visited);
				}
			}

			searcher.new_board();
			fclose(fd);
			/*
			move
			*/
		} else {
			/*parse opponent's move and make it*/
			str_mov(move,command);
			if(searcher.is_legal(move)) {
				searcher.do_move(move);
				do_search = true;
			} else {
				print("Error (unknown command): %s\n", command);
			}

			/*move recieved while pondering*/
			if(SEARCHER::chess_clock.pondering) {
				SEARCHER::chess_clock.pondering = false;
				if(SEARCHER::chess_clock.infinite_mode) {
					if(move == SEARCHER::expected_move) {
						print("ponder hit\n");
						SEARCHER::chess_clock.infinite_mode = false;
						SEARCHER::chess_clock.set_stime(searcher.hply);
						SEARCHER::chess_clock.search_time += int(0.5 * (get_time() - SEARCHER::start_time));
						return true;
					} else {
						print("ponder miss\n");
					}
				}
				return false;
			}
			/*end*/
		}
		/*In analysis mode search only if board is changed*/
		if(SEARCHER::analysis_mode) {
			if(hash_in != searcher.hash_key)
				return false;
		}
		/*Do search only when "go","analyze",or a move is input.
		Also don't bother if game is finished.*/
		if(!do_search)
			continue;
		if(result != R_UNKNOWN)
			continue;

		/*
		Analyze mode
		*/
		if(SEARCHER::analysis_mode) {
			do {
				SEARCHER::chess_clock.infinite_mode = true;
				main_searcher->COPY(&searcher);
				main_searcher->find_best();
				SEARCHER::chess_clock.infinite_mode = false;
				/*search is finished without abort flag -> book moves etc*/
				if(!SEARCHER::abort_search) {
					SEARCHER::abort_search = 1;
					return true;
				}
			} while(SEARCHER::analysis_mode);
			continue;
		}
		/*
		Play mode
		*/
		if(SEARCHER::scorpio == searcher.player) {
REDO1:
			/*check result before searching for our move*/
			if(SEARCHER::scorpio != searcher.player) continue;
			result = searcher.print_result(true);
			if(result != R_UNKNOWN) continue;

			/*search*/
			main_searcher->COPY(&searcher);
			move = main_searcher->find_best();
REDO2:
			/*we have a move, make it*/
			if(SEARCHER::scorpio != searcher.player) continue;
			if(move) {
				searcher.do_move(move);
				/*
				resign
				*/
				if((SEARCHER::root_score < -SEARCHER::resign_value)) {
					SEARCHER::resign_count++;
				} else {
					SEARCHER::resign_count = 0;
				}
				if(SEARCHER::resign_count == 3) {
					mov_strx(move,mv_str);
					print("resign\n");
					continue;
				}
				/*
				send move and result
				*/
				result = searcher.print_result(false);
				if(result == R_DRAW) 
					print("offer draw\n");
				mov_strx(move,mv_str);
				print_log("<%012d>",get_time() - scorpio_start_time);
				print("move %s\n",mv_str);
				if(result != R_UNKNOWN) {
					searcher.print_result(true);
					continue;
				}
				/*
				Pondering
				*/
				if(ponder) {

					SEARCHER::chess_clock.pondering = true;

					/*get move from recent pv*/
					if(main_searcher->stack[0].pv_length > 1) {
						move = main_searcher->stack[0].pv[1];
						/*try short search if above fails*/
					} else  {
						print("Pondering for opponent's move ...\n");
						main_searcher->COPY(&searcher);
						move = main_searcher->find_best();
						/*interrupted*/
						if(!SEARCHER::chess_clock.pondering)
							goto REDO1;
					}
					/*ponder with the move*/
					if(move) {
						mov_str(move,mv_str);
						print("pondering after move [%s]\n",mv_str);
						SEARCHER::expected_move = move;
						SEARCHER::chess_clock.infinite_mode = true;
						main_searcher->COPY(&searcher);
						main_searcher->do_move(move);
						move = main_searcher->find_best();

						/*ponder hit*/
						if(!SEARCHER::chess_clock.infinite_mode)
							goto REDO2;
						SEARCHER::chess_clock.infinite_mode = false;

						/*ponder miss*/
						if(!SEARCHER::chess_clock.pondering)
							goto REDO1;
					}
					/*end*/

					SEARCHER::chess_clock.pondering = false;
				}
				/*
				End pondering
				*/
			}
		}
	}
	return true;
}

/*
initilization file
*/
static bool load_ini() {
	char   buffer[MAX_FILE_STR];
	char*  commands[MAX_STR];
	char   temp[MAX_STR];


	FILE* fd = fopen("scorpio.ini","r");
	if(!fd) {
		print("Scorpio.ini not found!\n");
		return false;
	}

	strcpy(buffer,"");
	while(fgets(temp,MAX_STR,fd)) {
		if(temp[0] == '#' || temp[0] == '/' || temp[0] == '\n') continue;
		strcat(buffer,temp);
	}
	fclose(fd);

	print_log("========== scorpio.ini =======\n");
	print_log(buffer);
	print_log("==============================\n");

	commands[tokenize(buffer,commands)] = NULL;
	if(!parse_commands(commands))
		return false;

	return true;
}
