#include "scorpio.h"

#define VERSION "2.8.9 MCTS+NN"

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
int use_abdada_smp = 0;
#endif

#ifdef CLUSTER
VOLATILE int PROCESSOR::message_available = 0;
int PROCESSOR::MESSAGE_POLL_NODES = 200;
int PROCESSOR::CLUSTER_SPLIT_DEPTH = 8;
int PROCESSOR::host_id;
char PROCESSOR::host_name[256];
std::list<int> PROCESSOR::available_host_workers;
int PROCESSOR::help_messages = 0;
int PROCESSOR::prev_dest = -1;
const char *const PROCESSOR::message_str[] = {
    "QUIT","INIT","HELP","CANCEL","SPLIT","MERGE","PING","PONG",
    "GOROOT","RECORD_TT","PROBE_TT","PROBE_TT_RESULT"
};
int use_abdada_cluster = 0;
#endif
/*
static global variables of SEARCHER
*/
UBMP64 SEARCHER::root_score_st[MAX_MOVES];
int SEARCHER::history[14][64];
MOVE SEARCHER::refutation[14][64];
CHESS_CLOCK SEARCHER::chess_clock;
int SEARCHER::search_depth;
int SEARCHER::start_time;
int SEARCHER::scorpio;
int SEARCHER::pv_print_style;
int SEARCHER::root_score;
int SEARCHER::root_failed_low;
int SEARCHER::last_book_move;
int SEARCHER::first_search;
int SEARCHER::analysis_mode = false;
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
static int ht = 64;
static int eht = 8;
static int pht = 1;

static bool load_ini();
static void init_game();
static int self_play();

/*
load epd file
*/
static char* mem_epdfile = 0;
static int epdfile_count = 0;

static void load_epdfile(char* name) {
    FILE* fd = fopen(name, "r");
    if(!fd) {
        print("epd file %s not found!\n",name);
        return;
    }
    print("Started loading epd ...\n");
    fseek(fd, 0L, SEEK_END);
    long numbytes = ftell(fd);
    fseek(fd, 0L, SEEK_SET);
    print("Loading epd of size %.2f MB ...\n",double(numbytes)/(1024*1024));
    mem_epdfile = (char*)malloc(numbytes);
    fread(mem_epdfile, sizeof(char), numbytes, fd);
    print("Finished Loading epd file.\n");
    fclose(fd);
}

static void unload_epdfile() {
    free(mem_epdfile);
    mem_epdfile = 0;
    print("Unloaded epd!\n");
}

/*
load egbbs with a separate thread
*/
static VOLATILE bool egbb_is_loading = false;
static bool egbb_setting_changed = false;

static void wait_for_egbb() {
    while(egbb_is_loading) t_sleep(100);
}
static void CDECL egbb_thread_proc(void*) {
    int start = get_time();
    int egbb_cache_sizeb = (SEARCHER::egbb_cache_size * 1024 * 1024);
    SEARCHER::egbb_is_loaded = LoadEgbbLibrary(SEARCHER::egbb_path,egbb_cache_sizeb);
    int end = get_time();
    print("loading_time = %ds\n",(end - start) / 1000);
    egbb_is_loading = false;
}
static void load_egbbs() {
    /*Wait if we are still loading EGBBs*/
    if(egbb_setting_changed) {
        wait_for_egbb();
        egbb_setting_changed = false;
        egbb_is_loading = true;
        t_create(egbb_thread_proc,0);
    }
    wait_for_egbb();
}
/*
hash tables
*/
static void reset_ht() {
    UBMP32 size = 1,size_max = ht * ((1024 * 1024) / (2 * sizeof(HASH)));
    while(2 * size <= size_max) size *= 2;
    for(int i = 0;i < PROCESSOR::n_processors;i++) 
        processors[i]->reset_hash_tab(i,size);
    print("ht %d X %d = %.1f MB\n",2 * size,sizeof(HASH),(2 * size * sizeof(HASH)) / double(1024 * 1024));
}
static void reset_eht() {
    UBMP32 size = 1,size_max = eht * ((1024 * 1024) / (2 * sizeof(EVALHASH)));
    while(2 * size <= size_max) size *= 2;
    for(int i = 0;i < PROCESSOR::n_processors;i++) 
        processors[i]->reset_eval_hash_tab(size);
    print("eht %d X %d = %.1f MB\n",size,sizeof(EVALHASH),(2 * size * sizeof(EVALHASH)) / double(1024 * 1024));
}
static void reset_pht() {
    UBMP32 size = 1,size_max = pht * ((1024 * 1024) / (sizeof(PAWNHASH)));
    while(2 * size <= size_max) size *= 2;
    for(int i = 0;i < PROCESSOR::n_processors;i++) 
        processors[i]->reset_pawn_hash_tab(size);
    print("pht %d X %d = %.1f MB\n",size,sizeof(PAWNHASH),(size * sizeof(PAWNHASH)) / double(1024 * 1024));
}
/*
only winboard protocol support
*/
int CDECL main(int argc, char* argv[]) {
    char   buffer[MAX_FILE_STR];
    char*  commands[MAX_STR];

    /*init io*/
    init_io();
    
    /*init mpi*/
#ifdef CLUSTER
    PROCESSOR::init(argc,argv);
#endif
    
    /*tell winboard to wait*/
    print("feature done=0\n");
    
    /*init game*/
    init_game();

    /*
     * Parse scorpio.ini file which contains settings of scorpio. 
     * Search/eval execution commands should not be put there.
     */
    load_ini();

    /*
     * Initialize MPI
     */
#ifdef CLUSTER
    if(PROCESSOR::host_id == 0) {
#endif
        /*
         * Parse commands from the command line
         */
        strcpy(buffer,"");
        for(int i = 1;i < argc;i++) {
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
    return 0;
}

/*
initialize game
*/
void init_game() {
#ifdef PARALLEL
    l_create(lock_smp);
#endif
    scorpio_start_time = get_time();
    PROCESSOR::n_idle_processors = 0;
    PROCESSOR::n_processors = 1;
    PROCESSOR::set_main();
    main_searcher = processors[0]->searcher;
    SEARCHER::egbb_is_loaded = false;
    initmagicmoves();
    SEARCHER::pre_calculate();
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
    "gmse -- Compute gradient of mean squared error. Takes same arguments as mse.",
    "jacobian -- Compute jacobian of evaluation.",
    "hard -- Turn on pondering (thinking on the opponent's time or permanent brain).",
    "help -- Produce this listing of supported commands.",
    "history -- Debugging command to print game history.",
    "ht -- Set hash table size in megabytes.",
    "level -- level <MPS> <BASE> <INC> {Set time controls}.",
    "log -- turn loggin on/onff.",
    "merge <book1> <book2> <book> <w1> <w2> \n\tMerge two books with weights <w1> and <w2> and save restult in book.",
    "mirror -- Debugging command to mirror the current board.",
    "moves -- Debugging command to print all possible moves for the current board.",
    "mse -- mse <frac> <seed> Calculate mean squared error of evaluaiton/search result with actual result.",
    "       <frac>  -- Mini-batch size = fraction of positions to consider (0 - 1).",
    "       <seed>  -- Random number seed.",
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
    "param_group -- param_group <N> sets parameter group to tune",
    "zero_params -- zeros all evaluation parameters",
    "tune -- Tune the evaluation function. Takes same arguments as mse.",
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
            print("feature smp=0 memory=0\n");
            print("feature option=\"log -check %d\"\n",log_on);
            print("feature option=\"clear_hash -button\"\n");
            print("feature option=\"resign -spin %d 100 30000\"\n",SEARCHER::resign_value);
            print("feature option=\"cores -spin 1 1 %d\"\n", MAX_CPUS);
            print("feature option=\"ht -spin %d 1 131072\"\n",ht);
            print("feature option=\"eht -spin %d 1 16384\"\n",eht);
            print("feature option=\"pht -spin %d 1 256\"\n",pht);
            print("feature option=\"egbb_path -path %s\"\n", SEARCHER::egbb_path);
            print("feature option=\"nn_path -path %s\"\n", SEARCHER::nn_path);
            print("feature option=\"egbb_cache_size -spin %d 1 16384\"\n", SEARCHER::egbb_cache_size);
            print("feature option=\"egbb_load_type -spin %d 0 3\"\n", SEARCHER::egbb_load_type);
            print("feature option=\"egbb_depth_limit -spin %d 0 %d\"\n", SEARCHER::egbb_depth_limit, MAX_PLY);
            print("feature option=\"egbb_ply_limit_percent -spin %d 0 100\"\n", SEARCHER::egbb_ply_limit_percent);
            print("feature option=\"n_devices -spin %d 1 128\"\n",SEARCHER::n_devices);
            print("feature option=\"device_type -combo *CPU /// GPU \"\n");
            print("feature option=\"delay -spin %d 0 1000\"\n",SEARCHER::delay);
            print("feature option=\"float_type -combo FLOAT /// *HALF  /// INT8 \"\n");
            print_search_params();
            print_mcts_params();
#ifdef TUNE
            print_eval_params();
#endif
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
            main_searcher->print_status();
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
        } else if(!strcmp(command,"sv")) {
            SEARCHER::chess_clock.max_st = MAX_NUMBER;
            SEARCHER::chess_clock.max_visits = atoi(commands[command_num++]);
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
            searcher.print_game(result);
        } else if(!strcmp(command,"quit")) {
            print("Bye Bye\n");
            PROCESSOR::exit_scorpio(EXIT_SUCCESS);
        } else if (!strcmp(command, "clear_hash")) {
            PROCESSOR::clear_hash_tables();
        } else if (!strcmp(command, "new")) {
            load_egbbs();
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
            ht = atoi(commands[command_num++]);
            reset_ht();
        } else if(!strcmp(command,"pht")) {
            pht = atoi(commands[command_num++]);
            reset_pht();
        } else if(!strcmp(command,"eht")) {
            eht = atoi(commands[command_num++]);
            reset_eht();
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
            egbb_setting_changed = true;
            strcpy(SEARCHER::egbb_path,commands[command_num]);
            command_num++;
        } else if(!strcmp(command, "egbb_cache_size")) {
            egbb_setting_changed = true;
            SEARCHER::egbb_cache_size = atoi(commands[command_num]);
            command_num++;
        } else if(!strcmp(command, "egbb_load_type")) {
            egbb_setting_changed = true;
            SEARCHER::egbb_load_type = atoi(commands[command_num]);
            command_num++;
        } else if(!strcmp(command, "egbb_depth_limit")) {
            SEARCHER::egbb_depth_limit = atoi(commands[command_num]);
            command_num++;
        } else if(!strcmp(command, "egbb_ply_limit_percent")) {
            SEARCHER::egbb_ply_limit_percent = atoi(commands[command_num]);
            command_num++;
        } else if (!strcmp(command, "use_nn")) {
            if(!strcmp(commands[command_num],"on") ||
                !strcmp(commands[command_num],"1"))
                SEARCHER::use_nn = true;
            else
                SEARCHER::use_nn = false;
            command_num++;
        } else if(!strcmp(command, "nn_path")) {
            strcpy(SEARCHER::nn_path,commands[command_num]);
            command_num++;
        } else if(!strcmp(command, "n_devices")) {
            SEARCHER::n_devices = atoi(commands[command_num]);
            command_num++;
        } else if(!strcmp(command, "delay")) {
            SEARCHER::delay = atoi(commands[command_num]);
            command_num++;
        } else if(!strcmp(command, "float_type")) {
            command = commands[command_num++];
            if(!strcmp(command,"FLOAT")) SEARCHER::float_type = 0;
            else if(!strcmp(command,"HALF")) SEARCHER::float_type = 1;
            else SEARCHER::float_type = 2;
        } else if(!strcmp(command, "device_type")) {
            command = commands[command_num++];
            if(!strcmp(command,"CPU")) SEARCHER::device_type = 0;
            else  SEARCHER::device_type = 1;
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
        } else if (!strcmp(command,"pgn_to_epd")) {
            char source[1024] = "book.pgn",dest[1024] = "book.epd";

            int k = 0;
            while(true) {
                command = commands[command_num++];
                if(!command) break;
                if(k == 0) strcpy(source,command);
                else if(k == 1) strcpy(dest,command);
                k++;
            }
            searcher.pgn_to_epd(source,dest);
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
        } else if(check_mcts_params(commands,command,command_num)) {
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
            SEARCHER::pv_print_style = atoi(commands[command_num++]);
        } else if(!strcmp(command,"perft")) {
            clock_t start,end;
            int depth = atoi(commands[command_num++]);
            start = clock();
            UBMP64 nodes = searcher.perft(depth);
            end = clock();
            print("\nnodes " FMT64 "\n",nodes);
            print("time %.2f sec\n",(end - start) / 1000.0f);
        } else if(!strcmp(command,"score")) {
            int score;
            if(SEARCHER::egbb_is_loaded && searcher.all_man_c <= MAX_EGBB) {
                searcher.probe_bitbases(score);
                print("%d\n",score);
            } else {
                score = searcher.eval();
                print("%d\n",score);
            }
        } else if(!strcmp(command,"loadepd")) {
            load_epdfile(commands[command_num++]);
        } else if(!strcmp(command,"unloadepd")) {
            unload_epdfile();
#ifdef TUNE
        } else if(!strcmp(command,"param_group")) {
            int parameter_group = atoi(commands[command_num++]);
            init_parameters(parameter_group);
        } else if(!strcmp(command,"zero_params")) {
            zero_params();
            write_eval_params();
#endif
        } else if(!strcmp(command,"selfplay")) {
            int wins = 0, losses = 0, draws = 0;
            int N = atoi(commands[command_num++]),res;
            char FEN[MAX_STR];
            FILE* fw = fopen(commands[command_num++],"w");

            searcher.get_fen(FEN);
            print("Starting %d selfplay games\n",N);
            for(int i = 0;i < N;i++) {
                res = self_play();
                if(res == R_DRAW) draws++;
                else if(res == R_WWIN) wins++;
                else if(res == R_BWIN) losses++;
                searcher.print_game(res,fw);
                print("+ %d - %d = %d\n",wins,losses,draws);
                searcher.set_board(FEN);
            }

            fclose(fw);
            /*********************************************
            *     Processing epd files                  *
            *********************************************/
        } else if (!strcmp(command, "runeval") || 
                   !strcmp(command, "runevalepd") || 
                   !strcmp(command, "runsearch") ||
                   !strcmp(command, "runsearchepd") ||
                   !strcmp(command, "runquietepd") ||
                   !strcmp(command, "jacobian") ||
                   !strcmp(command, "mse") ||
                   !strcmp(command, "gmse") ||
                   !strcmp(command, "tune")
                   ) {
            load_egbbs();

            char input[MAX_STR],fen[MAX_STR];
            char* words[100];
            double frac = 1;
            int sc,sce,test,visited,result,nwords = 0;
            static const int DRAW_MARGIN = 35;
            enum {RUNEVAL = 0, RUNEVALEPD, RUNSEARCH, RUNSEARCHEPD, RUNQUIETEPD, JACOBIAN, MSE, GMSE, TUNE};

            if(!strcmp(command,"runeval")) test = RUNEVAL;
            else if(!strcmp(command,"runevalepd")) test = RUNEVALEPD;
            else if(!strcmp(command,"runsearch")) test = RUNSEARCH;
            else if(!strcmp(command,"runsearchepd")) test = RUNSEARCHEPD;
            else if(!strcmp(command,"runquietepd")) test = RUNQUIETEPD;
            else if(!strcmp(command,"jacobian")) test = JACOBIAN;
            else if(!strcmp(command,"mse")) test = MSE;
            else if(!strcmp(command,"gmse")) test = GMSE;
            else  test = TUNE;

            /*open file*/
            bool getfen = ((test <= JACOBIAN) 
#ifdef TUNE
                || (test >= MSE && !has_jacobian())
#endif
                );

            FILE *fd = 0, *fw = 0;
            if(getfen) {
#ifndef _WIN32
                if(mem_epdfile)
                    fd = fmemopen(mem_epdfile, strlen(mem_epdfile), "r");
                else 
#endif
                {
                    fd = fopen(commands[command_num++],"r");
                    if(!fd) {
                        print("epd file not found!\n");
                        continue;
                    }
                }
            }
            if(test == RUNEVALEPD || test == RUNSEARCHEPD || test == RUNQUIETEPD) {
                fw = fopen(commands[command_num++],"w");
            }
            if(!epdfile_count) {
                while(fgets(input,MAX_STR,fd))
                    epdfile_count++;
                rewind(fd);
            }
#ifdef TUNE
            /*set additional parameters of tune,mse & gmse*/
            if(test >= MSE) {
                frac = atof(commands[command_num++]);
                int randseed = atoi(commands[command_num++]);
                srand(randseed);
            }

            /*allocate jacobian*/
            if(test == JACOBIAN) {
                allocate_jacobian(epdfile_count);
                print("Computing jacobian matrix of evaluation function ...\n");
            }

            /*allocate arrays for SGD*/
            double *gse, *gmse, *dmse, *params, mse;
            const double gamma = 0.1, alpha = 1e4;
            int nSize = nParameters + nModelParameters;
            
            if(test >= GMSE) {
                gse = (double*) malloc(nSize * sizeof(double));
                gmse = (double*) malloc(nSize * sizeof(double));
                dmse = (double*) malloc(nSize * sizeof(double));
                params = (double*) malloc(nSize * sizeof(double));
                memset(dmse,0,nSize * sizeof(double));
                readParams(params);
            }
#endif
            /*Print headers*/
            if(test <= RUNSEARCH) {
                if(SEARCHER::pv_print_style == 0) 
                    print("******************************************\n");
                else if(SEARCHER::pv_print_style == 1)
                    print("\n\t\tNodes     Time      NPS      splits     bad"
                    "\n\t\t=====     ====      ===      ======     ===\n");
            }
            SEARCHER::pre_calculate();
            PROCESSOR::clear_hash_tables();

            /*Mini-batch loop*/
            for(int iter = 0;;iter++) {

                /*loop through all positions*/
                visited = 0;
#ifdef TUNE
                mse = 0.0;
                if(test >= GMSE)
                    memset(gmse,0,nSize * sizeof(double));
#endif
                for(int cnt = 0;cnt < epdfile_count;cnt++) {
                    /*read line*/
                    if(getfen) {
                        if(!fgets(input,MAX_STR,fd))
                            continue;
                    }
                    /*Sample a fraction of total postions: This is called a mini-batch gradient
                     descent with bootstrap sampling. In the standard mini-batch GD the sampling
                     of training positions is done without replacement.*/
                    if(test >= MSE && frac > 1e-6) {
                        double r = double(rand()) / RAND_MAX;
                        if(r >= frac) continue;
                    }
                    visited++;
                    /*decode fen*/
                    if(getfen) {
                        input[strlen(input) - 1] = 0;
                        nwords = tokenize(input,words) - 1;
                        strcpy(fen,words[0]);
                        strcat(fen," ");
                        strcat(fen,words[1]);
                        strcat(fen," ");
                        strcat(fen,words[2]);
                        strcat(fen," ");
                        strcat(fen,words[3]);
                        strcat(fen," ");
                        searcher.set_board(fen);
                        SEARCHER::scorpio = searcher.player;
                    }

                    switch(test) {
                    case RUNEVAL:
                    case RUNEVALEPD:
                        sc = searcher.eval();

                        if(test == RUNEVAL) {
                            searcher.mirror();
                            sce = searcher.eval();
                            if(sc == sce)
                                print("*%d* %d\n",visited,sc);
                            else {
                                print("*****WRONG RESULT*****\n");
                                print("[ %s ] \nsc = %6d sc1 = %6d\n",fen,sc,sce);
                                print("**********************\n");
                            }
                        }
                        if(test == RUNEVALEPD) {
                            int res;
                            if(sc > DRAW_MARGIN) res = 1;
                            else if( sc < -DRAW_MARGIN) res = -1;
                            else res = 0;
                            if(searcher.player == black) res = -res;
                            if(res == 1)
                                fprintf(fw, "%s 1-0\n", fen);
                            else if(res == -1)
                                fprintf(fw, "%s 0-1\n", fen);
                            else
                                fprintf(fw, "%s 1/2-1/2\n", fen);
                        }
                        break;
                    case RUNSEARCH:
                    case RUNSEARCHEPD:
                    case RUNQUIETEPD:
                        PROCESSOR::clear_hash_tables();
                        main_searcher->COPY(&searcher);
                        move = main_searcher->find_best();

                        if(test == RUNSEARCH) {
                            if(SEARCHER::pv_print_style == 0) 
                                print("********** %d ************\n",visited);
                        } else if(test == RUNSEARCHEPD) {
                            sc = main_searcher->pstack->best_score;
                            int res;
                            if(sc > DRAW_MARGIN) res = 1;
                            else if( sc < -DRAW_MARGIN) res = -1;
                            else res = 0;
                            if(searcher.player == black) res = -res;
                            if(res == 1)
                                fprintf(fw, "%s 1-0\n", fen);
                            else if(res == -1)
                                fprintf(fw, "%s 0-1\n", fen);
                            else
                                fprintf(fw, "%s 1/2-1/2\n", fen);
                        } else {
                            if(!is_cap_prom(move)) {
                                if(!strncmp(words[nwords - 1],"1-0",3))  
                                    fprintf(fw, "%s 1-0\n", fen);
                                else if(!strncmp(words[nwords - 1],"0-1",3)) 
                                    fprintf(fw, "%s 0-1\n", fen);
                                else if(!strncmp(words[nwords - 1],"1/2-1/2",7)) 
                                    fprintf(fw, "%s 1/2-1/2\n", fen);
                            }
                        }
                        break;
                    case JACOBIAN:
                    case MSE:
                    case GMSE:
                    case TUNE:
                        if(getfen) {
                            if(!strncmp(words[nwords - 1],"1-0",3)) result = 1;
                            else if(!strncmp(words[nwords - 1],"0-1",3)) result = -1;
                            else if(!strncmp(words[nwords - 1],"1/2-1/2",7)) result = 0;
                            else {
                                print("Position %d not labeled: fen %s\n",visited,fen);
                                continue;
                            }
                            if(searcher.player == black) 
                                result = -result;
                        }
#ifdef TUNE
                        if(test == JACOBIAN) {
                            compute_jacobian(&searcher,cnt,result);
                        } else {
                            /*compute evaluation from the stored jacobian*/
                            double se;
                            if(getfen) {
                                se = searcher.get_root_search_score();
                            } else {
                                se = eval_jacobian(cnt,result,params);
                            }
                            /*compute loss function (log-likelihood) or its gradient*/
                            if(test == MSE) {
                                se = get_log_likelihood(result,se);
                                mse += (se - mse) / visited;
                            } else  {
                                get_log_likelihood_grad(&searcher,result,se,gse,cnt);
                                for(int i = 0;i < nSize;i++)
                                    gmse[i] += (gse[i] - gmse[i]) / visited;
                            }
                        }
#endif
                        break;
                    }
                }

#ifdef TUNE
                /*Update parameters based on gradient of loss function computed 
                  over the current mini-batch*/
                if(test == JACOBIAN) {
                    print("Computed jacobian for %d positions.\n",visited);
                } else if(test == MSE) {
                    print("%.9e\n",mse);
                } else if(test == GMSE) {
                    for(int i = 0;i < nSize;i++)
                        print("%.9e ",gmse[i]);
                    print("\n");
                } else if(test == TUNE) {
                    double normg = 0;
                    for(int i = 0;i < nSize;i++) {
                        dmse[i] = -gmse[i] + gamma * dmse[i];
                        params[i] += alpha * dmse[i];
                        normg += pow(gmse[i],2.0);
                    }
                    bound_params(params);
                    writeParams(params);
                    print("%d. %.6e\n",iter,normg);
                    if(iter % 40 == 0)
                        write_eval_params();
                    if(getfen) {
                        SEARCHER::pre_calculate();
                        rewind(fd);
                    }
                }
#endif
                if(test != TUNE) break;
            }
#ifdef TUNE
            if(test >= 4) {
                free(gse);
                free(gmse);
                free(dmse);
                free(params);
            }
#endif
            searcher.new_board();
            if(fd) fclose(fd);
            if(fw) fclose(fw);

            /*********************************************
            *  We process all other commands as moves   *
            *********************************************/
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

        /*Wait if we are still loading EGBBs*/
        load_egbbs();

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
Get search score
*/
int SEARCHER::get_root_search_score() {
    int sce;
    if(SEARCHER::chess_clock.max_sd == 0) {
        //evaluation score
        sce = eval();
    } else if(SEARCHER::chess_clock.max_sd == 1) {
        //quiescence search score
        if((player == white && attacks(black,plist[wking]->sq)) ||
           (player == black && attacks(white,plist[bking]->sq)) ){
            sce = eval();
        } else {
            main_searcher->COPY(this);
            main_searcher->pstack->node_type = PV_NODE;
            main_searcher->pstack->search_state = NORMAL_MOVE;
            main_searcher->pstack->alpha = -MATE_SCORE;
            main_searcher->pstack->beta = MATE_SCORE;
            main_searcher->pstack->depth = 0;
            main_searcher->pstack->qcheck_depth = 0;    
            main_searcher->qsearch();
            sce = main_searcher->pstack->best_score;
        }
    } else {
        //regular search score
        main_searcher->COPY(this);
        main_searcher->find_best();
        sce = main_searcher->pstack->best_score;
    }
    return sce;
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
/*
Selfplay
*/
static int self_play() {
    MOVE move;
    SEARCHER::resign_count = 0;
    result = R_UNKNOWN;

    while(true) {

        /*check result before searching for our move*/
        result = searcher.print_result(false);
        if(result != R_UNKNOWN) 
            return result;

        /*search*/
        main_searcher->COPY(&searcher);
        move = main_searcher->find_best();

        /*we have a move, make it*/
        if(move != 0) {
            searcher.do_move(move);

            /*resign*/
            if((SEARCHER::root_score < -SEARCHER::resign_value)) {
                SEARCHER::resign_count++;
            } else {
                SEARCHER::resign_count = 0;
            }
            if(SEARCHER::resign_count == 3) {
                return (R_WWIN + (searcher.opponent == black));
            }
        } else {
            return R_DRAW;
        }
    }
}

