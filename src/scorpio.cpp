#include "scorpio.h"

#define VERSION "3.0.9"

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
bool is_selfplay = false;
int  PROTOCOL = CONSOLE;

/*
parallel search
*/
PPROCESSOR processors[MAX_CPUS] = {0};
int PROCESSOR::n_processors;
int PROCESSOR::n_cores;
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
int SEARCHER::start_time_o;
int SEARCHER::scorpio;
int SEARCHER::pv_print_style;
int SEARCHER::root_score;
int SEARCHER::old_root_score;
int SEARCHER::root_failed_low;
float SEARCHER::time_factor;
int SEARCHER::last_book_move = 0;
int SEARCHER::first_search;
int SEARCHER::analysis_mode = false;
int SEARCHER::show_full_pv;
int SEARCHER::abort_search;
unsigned int SEARCHER::poll_nodes;
MOVE SEARCHER::expected_move;
int SEARCHER::resign_value;
int SEARCHER::resign_count;
unsigned int SEARCHER::average_pps = 0;
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
    int nn_cache_sizeb = (SEARCHER::nn_cache_size * 1024 * 1024);
    LoadEgbbLibrary(SEARCHER::egbb_path,egbb_cache_sizeb,nn_cache_sizeb);
    int end = get_time();
    print("loading_time = %ds\n",(end - start) / 1000);
    egbb_is_loading = false;
}
static void load_egbbs() {
    /*Wait if we are still loading EGBBs*/
    if(egbb_setting_changed && !SEARCHER::egbb_is_loaded) {
        egbb_setting_changed = false;
        egbb_is_loading = true;
        pthread_t dummy;
        t_create(dummy,egbb_thread_proc,0);
        (void)dummy;
    }
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
    print("eht %d X %d X %d = %.1f MB\n",size,sizeof(EVALHASH),PROCESSOR::n_processors,
        PROCESSOR::n_processors * (2 * size * sizeof(EVALHASH)) / double(1024 * 1024));
}
static void reset_pht() {
    UBMP32 size = 1,size_max = pht * ((1024 * 1024) / (sizeof(PAWNHASH)));
    while(2 * size <= size_max) size *= 2;
    for(int i = 0;i < PROCESSOR::n_processors;i++) 
        processors[i]->reset_pawn_hash_tab(size);
    print("pht %d X %d X %d = %.1f MB\n",size,sizeof(PAWNHASH),PROCESSOR::n_processors,
        PROCESSOR::n_processors * (size * sizeof(PAWNHASH)) / double(1024 * 1024));
}
/*
main
*/
int CDECL main(int argc, char* argv[]) {
    char   buffer[4*MAX_FILE_STR];
    char*  commands[4*MAX_STR];

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
     * Host 0 processes command line
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

        /* start loading egbbs */
        load_egbbs();

        /*
         * Parse commands from stdin.
         */
        print_log("==============================\n");
        while(true) {
            if(!read_line(buffer))
                goto END;
            if(PROTOCOL == UCI) {
                std::string s(buffer);
                while (s.find("value") != std::string::npos)
                    s.replace(s.find("value"), 5, " ");
                strcpy(buffer,s.c_str());
            }
            commands[tokenize(buffer,commands)] = NULL;
            if(!parse_commands(commands))
                goto END;
        }
#ifdef  CLUSTER
    } else {
        /* Delete the log file.*/
        if(!log_on)
            remove_log_file();

        /* start loading egbbs */
        load_egbbs();

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
    PROCESSOR::n_cores = get_number_of_cpus();
    PROCESSOR::n_idle_processors = 0;
    PROCESSOR::n_processors = 1;
    PROCESSOR::set_main();
    main_searcher = processors[0]->searcher;
    SEARCHER::egbb_is_loaded = 0;
    initmagicmoves();
    SEARCHER::pre_calculate();
    searcher.new_board();
    SEARCHER::scorpio = black;
    SEARCHER::first_search = true;
    SEARCHER::old_root_score = 0;
    SEARCHER::root_score = 0;
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
    "egbb_path -- The path to egbbdll.so/dll file.",
    "egbb_files_path -- The path to the actual endgame tablebase files.",
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
    "uci -- Request uci mode.",
    "xboard -- Request xboard mode.",
    NULL
};
/*
Engine options
*/
int is_checked(const char* str) {
    if(!strcmp(str,"on") || !strcmp(str,"true") || !strcmp(str,"1"))
       return 1;
    else
       return 0;
}
void print_spin(const char* name, int def, int min, int max) {
    if(PROTOCOL == UCI)
        print("option name %s type spin default %d min %d max %d\n", name, def, min, max);
    else
        print("feature option=\"%s -spin %d %d %d\"\n", name, def, min, max);
}
void print_check(const char* name, int def) {
    if(PROTOCOL == UCI)
        print("option name %s type check default %s\n", name, def ? "true" : "false");
    else
        print("feature option=\"%s -check %d\"\n", name, def);
}
void print_button(const char* name) {
    if(PROTOCOL == UCI)
        print("option name %s type button\n", name);
    else
        print("feature option=\"%s -button\"\n", name);
}
void print_path(const char* name, const char* path) {
    if(PROTOCOL == UCI)
        print("option name %s type string default %s\n", name, path);
    else
        print("feature option=\"%s -path %s\"\n", name, path);
}
void print_combo(const char* name, const char* combo[], int def, int N) {
    if(PROTOCOL == UCI) {
        print("option name %s type combo default %s", name, combo[def]);
        for(int i = 0; i < N; i++)
            print(" var %s",combo[i]);
        print("\n");
    } else {
        print("feature option=\"%s -combo", name);
        for(int i = 0; i < N; i++) {
            if(i == def) print(" *%s",combo[i]);
            else print(" %s",combo[i]);
            if(i < N - 1) print(" ///");
        }
        print("\"\n");
    }
}
static void print_options() {
    static const char* dtype[] = {"CPU", "GPU"};
    static const char* ftype[] = {"FLOAT", "HALF", "INT8"};
    print_check("log",log_on);
    print_button("clear_hash");
    print_spin("resign",SEARCHER::resign_value,100,30000);
    print_spin("mt",PROCESSOR::n_processors,1,MAX_CPUS);
    print_spin("ht",ht,1,131072);
    print_spin("eht",eht,1,16384);
    print_spin("pht",pht,1,256);
    print_path("egbb_path",SEARCHER::egbb_path);
    print_path("egbb_files_path",SEARCHER::egbb_files_path);
    print_path("nn_path",SEARCHER::nn_path);
    print_path("nn_path_e",SEARCHER::nn_path_e);
    print_path("nn_path_m",SEARCHER::nn_path_m);
    print_spin("egbb_cache_size",SEARCHER::egbb_cache_size,1,16384);
    print_spin("egbb_load_type",SEARCHER::egbb_load_type,0,3);
    print_spin("egbb_depth_limit",SEARCHER::egbb_depth_limit,0,MAX_PLY);
    print_spin("egbb_ply_limit_percent",SEARCHER::egbb_ply_limit_percent,0,100);
    print_spin("nn_cache_size",SEARCHER::nn_cache_size,1,16384);
    print_spin("n_devices",SEARCHER::n_devices,1,128);
    print_combo("device_type",dtype,SEARCHER::device_type,2);
    print_spin("delay",SEARCHER::delay,0,1000);
    print_combo("float_type",ftype,SEARCHER::float_type,3);
    print_spin("nn_type",SEARCHER::nn_type,0,10);
    print_spin("nn_type_e",SEARCHER::nn_type_e,-1,10);
    print_spin("nn_type_m",SEARCHER::nn_type_m,-1,10);
    print_spin("nn_man_e",SEARCHER::nn_man_e,0,32);
    print_spin("nn_man_m",SEARCHER::nn_man_m,0,32);
    print_check("wdl_head",SEARCHER::wdl_head);
    print_check("wdl_head_m",SEARCHER::wdl_head_m);
    print_check("wdl_head_e",SEARCHER::wdl_head_e);
    print_spin("win_weight",win_weight,0,1000);
    print_spin("draw_weight",draw_weight,0,1000);
    print_spin("loss_weight",loss_weight,0,1000);
}
/**
* Internal scorpio commands
*/
bool internal_commands(char** commands,char* command,int& command_num) {
    if (!strcmp(command, "xboard")) {
        PROTOCOL = XBOARD;
    } else if(!strcmp(command,"uci")) {
        PROTOCOL = UCI;
        print("id name Scorpio %s\n",VERSION);
        print("id author Daniel Shawul\n");
        print_options();
        print_search_params();
        print_mcts_params();
#ifdef TUNE
        print_eval_params();
#endif
        print("uciok\n");
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
    } else if(!strcmp(command,"mt") || !strcmp(command,"cores") || !strcmp(command,"Threads") ) {
#ifdef PARALLEL
        if(strcmp(command,"mt") && montecarlo && SEARCHER::use_nn);
        else {
            int mt;
            if(!strcmp(commands[command_num],"auto"))
                mt = PROCESSOR::n_cores;
            else if(!strncmp(commands[command_num],"auto-",5)) {
                int r = atoi(&commands[command_num][5]);
                mt = PROCESSOR::n_cores - r;
            } else
                mt = atoi(commands[command_num]);
            mt = MIN(mt, MAX_CPUS);
            init_smp(mt);
            print("processors [%d]\n",PROCESSOR::n_processors);
        }
#endif
        command_num++;
        /*
        egbb
        */
#ifdef EGBB
    } else if(!strcmp(command, "egbb_path")) {
        egbb_setting_changed = true;
        strcpy(SEARCHER::egbb_path,commands[command_num]);
        strcpy(SEARCHER::egbb_files_path,SEARCHER::egbb_path);
        command_num++;
    } else if(!strcmp(command, "egbb_files_path")) {
        egbb_setting_changed = true;
        strcpy(SEARCHER::egbb_files_path,commands[command_num]);
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
    } else if(!strcmp(command, "nn_cache_size")) {
        egbb_setting_changed = true;
        SEARCHER::nn_cache_size = atoi(commands[command_num]);
        command_num++;
    } else if (!strcmp(command, "use_nn")) {
        if(!strcmp(commands[command_num],"on") ||
            !strcmp(commands[command_num],"1"))
            SEARCHER::use_nn = true;
        else
            SEARCHER::use_nn = false;
        SEARCHER::save_use_nn = SEARCHER::use_nn;
        command_num++;
    } else if(!strcmp(command, "nn_path")) {
        strcpy(SEARCHER::nn_path,commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nn_path_e")) {
        strcpy(SEARCHER::nn_path_e,commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nn_path_m")) {
        strcpy(SEARCHER::nn_path_m,commands[command_num]);
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
    } else if(!strcmp(command, "nn_type")) {
        SEARCHER::nn_type = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nn_type_e")) {
        SEARCHER::nn_type_e = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nn_type_m")) {
        SEARCHER::nn_type_m = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nn_man_e")) {
        SEARCHER::nn_man_e = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nn_man_m")) {
        SEARCHER::nn_man_m = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "wdl_head")) {
        SEARCHER::wdl_head = is_checked(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "wdl_head_m")) {
        SEARCHER::wdl_head_m = is_checked(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "wdl_head_e")) {
        SEARCHER::wdl_head_e = is_checked(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "win_weight")) {
        win_weight = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "draw_weight")) {
        draw_weight = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "loss_weight")) {
        loss_weight = atoi(commands[command_num]);
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
    } else if (!strcmp(command,"pgn_to_epd") ||
               !strcmp(command,"pgn_to_epd_score") ||
               !strcmp(command,"pgn_to_nn") ||
               !strcmp(command,"pgn_to_dat") ||
               !strcmp(command,"epd_to_score") ||
               !strcmp(command,"epd_to_nn") ||
               !strcmp(command,"epd_to_dat")
        ) {
        char source[1024],dest[1024];
        strcpy(source,commands[command_num++]);
        strcpy(dest,commands[command_num++]);

        load_egbbs();
        wait_for_egbb();

        int task;
        if(!strcmp(command,"pgn_to_epd"))
            task = 0;
        else if(!strcmp(command,"pgn_to_epd_score"))
            task = 1;
        else if(!strcmp(command,"pgn_to_nn"))
            task = 2;
        else if(!strcmp(command,"pgn_to_dat"))
            task = 3;
        else if(!strcmp(command,"epd_to_score"))
            task = 4;
        else if(!strcmp(command,"epd_to_nn"))
            task = 5;
        else
            task = 6;

        /*process pgn*/
        FILE* fb;
        if(task == 2)
            fb = fopen(dest,"wb");
        else
            fb = fopen(dest,"w");

        main_searcher->COPY(&searcher);

        if(task <= 3) {
            PGN pgn;
            pgn.open(source);
            main_searcher->worker_thread_all(&pgn,fb,task);
            pgn.close();
        } else {
            EPD epd;
            epd.open(source);
            main_searcher->worker_thread_all(&epd,fb,task);
            epd.close();
        }

        fclose(fb);

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
        if(is_checked(commands[command_num]))
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
        print("time %.2f sec\n",(double)(end - start) / CLOCKS_PER_SEC);
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
    } else if(!strcmp(command,"randomize")) {
        is_selfplay = true;
    } else if(!strcmp(command,"quit")) {
        print("Bye Bye\n");
        PROCESSOR::exit_scorpio(EXIT_SUCCESS);
    } else if(!strcmp(command,"selfplay")) {
        int wins = 0, losses = 0, draws = 0,res;
        char FEN[MAX_STR];
        int N = atoi(commands[command_num++]);
        char name[512];
        int unique_id = GETPID() + rand();
        sprintf(name,"%s.%d",commands[command_num++],unique_id);
        FILE* fw = fopen(name,"w");

        load_egbbs();
        wait_for_egbb();

        is_selfplay = true;
        searcher.get_fen(FEN);
        print("Starting %d selfplay games\n",N);
        for(int i = 0;i < N;i++) {
            res = self_play();
            if(res == R_DRAW) draws++;
            else if(res == R_WWIN) wins++;
            else if(res == R_BWIN) losses++;
            searcher.print_game(res,fw,"Training games",
                "ScorpioZero","ScorpioZero",wins+losses+draws);
            print("[%d] Games %d: + %d - %d = %d\n",GETPID(),
                wins+losses+draws,wins,losses,draws);
            searcher.set_board(FEN);
        }
        print("Finished\n");
        fclose(fw);
    } else if(!strcmp(command,"selfplayp")) {
        int N = atoi(commands[command_num++]);
        char name[512];
        int unique_id = GETPID() + rand();
        sprintf(name,"%s.%d",commands[command_num++],unique_id);
        FILE* fw = fopen(name,"w");
        sprintf(name,"%s.%d",commands[command_num++],unique_id);
        FILE* fw2 = fopen(name,"w");
        
        load_egbbs();
        wait_for_egbb();
        
        print("Starting %d selfplay games\n",N);
        is_selfplay = true;
        main_searcher->COPY(&searcher);
        main_searcher->self_play_thread_all(fw,fw2,N);

        print("Finished\n");
        fclose(fw);
        fclose(fw2);
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
        wait_for_egbb();

        MOVE move;
        char input[MAX_STR],fen[MAX_STR];
        char* words[100];
        double frac = 1;
        int sc,sce,test,visited,result,nwords = 0;
        static const int DRAW_MARGIN = 35;
        enum {RUNEVAL = 0, RUNEVALEPD, RUNSEARCH, RUNSEARCHEPD, RUNQUIETEPD, JACOBIAN, MSE, GMSE, TUNE_EVAL};

        if(!strcmp(command,"runeval")) test = RUNEVAL;
        else if(!strcmp(command,"runevalepd")) test = RUNEVALEPD;
        else if(!strcmp(command,"runsearch")) test = RUNSEARCH;
        else if(!strcmp(command,"runsearchepd")) test = RUNSEARCHEPD;
        else if(!strcmp(command,"runquietepd")) test = RUNQUIETEPD;
        else if(!strcmp(command,"jacobian")) test = JACOBIAN;
        else if(!strcmp(command,"mse")) test = MSE;
        else if(!strcmp(command,"gmse")) test = GMSE;
        else test = TUNE_EVAL;

        /*open file*/
        bool getfen = ((test <= JACOBIAN) 
#ifdef TUNE
            || (test >= MSE && !has_jacobian())
#endif
            );

        FILE *fd = 0, *fw = 0;
        if(getfen) {
#if !defined(_WIN32) && !defined(__ANDROID__)
            if(mem_epdfile)
                fd = fmemopen(mem_epdfile, strlen(mem_epdfile), "r");
            else 
#endif
            {
                fd = fopen(commands[command_num++],"r");
                if(!fd) {
                    print("epd file not found!\n");
                    return false;
                }
            }
        }
        if(test == RUNEVALEPD || test == RUNSEARCHEPD || test == RUNQUIETEPD)
            fw = fopen(commands[command_num++],"w");
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
            else if(SEARCHER::pv_print_style == 1) {
                if(montecarlo) {
                    print("\n\t\tVisits     Time        PPS      NNEPS"
                          "\n\t\t======     ====        ===      =====\n");
                } else {
                    print("\n\t\tNodes     Time        NPS   splits      bad"
                          "\n\t\t=====     ====        ===   ======      ===\n");
                }
            }
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
                    searcher.copy_root(main_searcher);

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
                            fprintf(fw, "%s 1-0 ", fen);
                        else if(res == -1)
                            fprintf(fw, "%s 0-1 ", fen);
                        else
                            fprintf(fw, "%s 1/2-1/2 ", fen);
                        fprintf(fw,"\n");
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
                case TUNE_EVAL:
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
            } else if(test == TUNE_EVAL) {
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
            if(test != TUNE_EVAL) break;
        }
#ifdef TUNE
        if(test >= GMSE) {
            free(gse);
            free(gmse);
            free(dmse);
            free(params);
        }
#endif
        searcher.new_board();
        if(fd) fclose(fd);
        if(fw) fclose(fw);
    } else {
        return false;
    }
    return true;
}
/**
 * xboard protocol
 */
int xboard_commands(char** commands,char* command,int& command_num,int& do_search) {
    if (!strcmp(command, "protover")) {
        print("feature name=1 myname=\"Scorpio %s\"\n",VERSION);
        print("feature sigint=0 sigterm=0\n");
        print("feature setboard=1 usermove=1 draw=0 colors=0\n");
        print("feature smp=0 memory=0 debug=1\n");
        print_options();
        print_search_params();
#ifdef TUNE
        print_eval_params();
#endif
        print_mcts_params();
        print("feature done=1\n");
        command_num++;
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
        SEARCHER::chess_clock.p_inc = 1000 * atoi(commands[command_num++]);
        SEARCHER::chess_clock.o_time = searcher.chess_clock.p_time;
        SEARCHER::chess_clock.o_inc = searcher.chess_clock.p_inc;
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
            return 0;
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
    } else if (!strcmp(command, "clear_hash")) {
        PROCESSOR::clear_hash_tables();
    } else if (!strcmp(command, "new")) {
        PROCESSOR::clear_hash_tables();
        searcher.new_board();
        result = R_UNKNOWN;
        SEARCHER::scorpio = black;
        SEARCHER::first_search = true;
        SEARCHER::old_root_score = 0;
        SEARCHER::root_score = 0;
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
    } else {
        if(PROTOCOL == XBOARD) {
            if(!strcmp(command,"usermove")) {
                command = commands[command_num++];
            } else {
                print("Error (unknown command): %s\n", command);
                return 2;
            }
        }
        /*parse opponent's move and make it*/
        MOVE move;
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
                    SEARCHER::chess_clock.set_stime(searcher.hply,true);
                    SEARCHER::chess_clock.search_time += 
                        int(0.5 * (get_time() - SEARCHER::start_time));
                    return 1;
                } else {
                    print("ponder miss\n");
                }
            }
            return 0;
        }
        /*end*/
    }
    return 2;
}
/*
uci protocol
*/
int uci_commands(char** commands,char* command,int& command_num,int& do_search) {
    if (!strcmp(command, "ucinewgame")) {
        PROCESSOR::clear_hash_tables();
        searcher.new_board();
        result = R_UNKNOWN;
        SEARCHER::scorpio = black;
        SEARCHER::first_search = true;
        SEARCHER::old_root_score = 0;
        SEARCHER::root_score = 0;
    } else if(!strcmp(command, "isready")) {
        wait_for_egbb();
        print("readyok\n");
    } else if(!strcmp(command,"position")) {
        command = commands[command_num++];
        if(command && !strcmp(command,"fen")) {    
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
        } else {
            searcher.new_board();
        }
        command = commands[command_num++];
        if(command && !strcmp(command,"moves")) {
            while(true) {
                command = commands[command_num++];
                if(!command) break;

                MOVE move;
                str_mov(move,command);
                if(searcher.is_legal(move)) {
                    searcher.do_move(move);
                } else {
                    print("Illegal move: %s\n", command);
                }
            }
        }
        result = R_UNKNOWN;
        SEARCHER::scorpio = neutral;
    } else if(!strcmp(command,"go")) {
        SEARCHER::scorpio = searcher.player;
        do_search = true;
        while(true) {
            command = commands[command_num++];
            if(!command) break;

            if(!strcmp(command,"infinite")) {
                SEARCHER::analysis_mode = true;
            } else if(!strcmp(command,"movetime")) {
                SEARCHER::chess_clock.max_sd = MAX_PLY;
                SEARCHER::chess_clock.max_st = atoi(commands[command_num++]);
            } else if(!strcmp(command,"depth")) {
                SEARCHER::chess_clock.max_st = MAX_NUMBER;
                SEARCHER::chess_clock.max_sd = atoi(commands[command_num++]);
            } else if(!strcmp(command,"nodes")) {
                SEARCHER::chess_clock.max_st = MAX_NUMBER;
                SEARCHER::chess_clock.max_visits = atoi(commands[command_num++]);
            } else if(!strcmp(command,"movestogo")) {
                SEARCHER::chess_clock.mps = atoi(commands[command_num++]);
                SEARCHER::chess_clock.max_st = MAX_NUMBER;
                SEARCHER::chess_clock.max_sd = MAX_PLY;
            } else if(!strcmp(command,"wtime")) {
                if(searcher.player == white)
                    SEARCHER::chess_clock.p_time = atoi(commands[command_num++]);
                else
                    SEARCHER::chess_clock.o_time = atoi(commands[command_num++]);
                SEARCHER::chess_clock.max_st = MAX_NUMBER;
                SEARCHER::chess_clock.max_sd = MAX_PLY;
                SEARCHER::chess_clock.mps = 0;
            } else if(!strcmp(command,"btime")) {
                if(searcher.player == white)
                    SEARCHER::chess_clock.o_time = atoi(commands[command_num++]);
                else
                    SEARCHER::chess_clock.p_time = atoi(commands[command_num++]);
                SEARCHER::chess_clock.max_st = MAX_NUMBER;
                SEARCHER::chess_clock.max_sd = MAX_PLY;
                SEARCHER::chess_clock.mps = 0;
            } else if(!strcmp(command,"winc")) {
                if(searcher.player == white)
                    SEARCHER::chess_clock.p_inc = atoi(commands[command_num++]);
                else
                    SEARCHER::chess_clock.o_inc = atoi(commands[command_num++]);
            } else if(!strcmp(command,"binc")) {
                if(searcher.player == white)
                    SEARCHER::chess_clock.o_inc = atoi(commands[command_num++]);
                else
                    SEARCHER::chess_clock.p_inc = atoi(commands[command_num++]);
            } else if(!strcmp(command,"ponder")) {
                ponder = true;
            } else if(!strcmp(command,"searchmoves")) {
            } else if(!strcmp(command,"mate")) {
            }
        }
    } else if (!strcmp(command, "stop")) {
        SEARCHER::abort_search = 1;
    } else if (!strcmp(command, "ponderhit")) {
        /*ponder hit*/
        if(SEARCHER::chess_clock.pondering) {
            ponder = false;
            SEARCHER::chess_clock.pondering = false;
            if(SEARCHER::chess_clock.infinite_mode) {
                searcher.do_move(SEARCHER::expected_move);
                SEARCHER::chess_clock.infinite_mode = false;
                SEARCHER::chess_clock.set_stime(searcher.hply,true);
                SEARCHER::chess_clock.search_time += 
                    int(0.5 * (get_time() - SEARCHER::start_time));
                return 1;
            }
            return 0;
        }
        /*end*/
    }

    return 2;
}
static bool send_bestmove(MOVE move) {
    char mv_str[10];
    /*
    resign
    */
    if(PROTOCOL == XBOARD || PROTOCOL == CONSOLE) {
        if((SEARCHER::root_score < -SEARCHER::resign_value)) {
            SEARCHER::resign_count++;
        } else {
            SEARCHER::resign_count = 0;
        }
        if(SEARCHER::resign_count == 3) {
            print("resign\n");
            return false;
        }
        if(result == R_DRAW) 
            print("offer draw\n");
    }
    /*
    send move and result
    */
    mov_strx(move,mv_str);
    print_log("<%012d>",get_time() - scorpio_start_time);
    if(PROTOCOL == UCI) {
        if(main_searcher->stack[0].pv_length > 1) {
            char mv_ponder_str[10];
            MOVE move_ponder = main_searcher->stack[0].pv[1];
            mov_strx(move_ponder,mv_ponder_str);
            print("bestmove %s ponder %s\n",mv_str, mv_ponder_str);
            SEARCHER::expected_move = move;
        } else
            print("bestmove %s\n",mv_str);
    } else {
        print("move %s\n",mv_str);
        if(result != R_UNKNOWN) {
            searcher.print_result(true);
            return false;
        }
    }
    return true;
}
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
        /**
        * process commands
        */
        do_search = false;
        if(internal_commands(commands,command,command_num)) {
        } else {
            int ret = 0;
            if(PROTOCOL == XBOARD || PROTOCOL == CONSOLE) 
                ret = xboard_commands(commands,command,command_num,do_search);
            else if(PROTOCOL == UCI)
                ret = uci_commands(commands,command,command_num,do_search);
            if(ret == 1) return true;
            else if(ret == 0) return false;
        }
        
        /********************************
         * Commands have been processed.
         * Now do one of the following: 
         *      - best move search
         *      - analyze mode
         *      - pondering
         ********************************/

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
        wait_for_egbb();

        /*
        Analyze mode
        */
        if(SEARCHER::analysis_mode) {
            do {
                SEARCHER::chess_clock.infinite_mode = true;
                main_searcher->COPY(&searcher);
                move = main_searcher->find_best();
                searcher.copy_root(main_searcher);
                SEARCHER::chess_clock.infinite_mode = false;
                /* send move in uci mode */
                if(PROTOCOL == UCI) {
                    SEARCHER::analysis_mode = false;
                    send_bestmove(move);
                }
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
            if(PROTOCOL == UCI)
                result = searcher.print_result(false);
            else
                result = searcher.print_result(true);
            if(result != R_UNKNOWN) continue;

            /*
            UCI protocol - search or ponder
            */
            if(PROTOCOL == UCI) {
                int pn = ponder;

                if(pn) {
                    SEARCHER::chess_clock.pondering = true;
                    SEARCHER::chess_clock.infinite_mode = true;
                }

                main_searcher->COPY(&searcher);
                move = main_searcher->find_best();
                searcher.copy_root(main_searcher);

                if(pn) {
                    SEARCHER::chess_clock.pondering = false;
                    SEARCHER::chess_clock.infinite_mode = false;
                    ponder = false;
                }

                if(move) {
                    if(!pn)
                        searcher.do_move(move);
                    send_bestmove(move);
                }

                continue;
            }

            /*
            Winboard protocol - search or ponder
            */
            main_searcher->COPY(&searcher);
            move = main_searcher->find_best();
            searcher.copy_root(main_searcher);
REDO2:
            /*we have a move, make it*/
            if(SEARCHER::scorpio != searcher.player) continue;
            if(move) {
                searcher.do_move(move);
                result = searcher.print_result(false);
                if(!send_bestmove(move)) continue;
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
                        print_info("Pondering for opponent's move ...\n");
                        main_searcher->COPY(&searcher);
                        move = main_searcher->find_best();
                        searcher.copy_root(main_searcher);
                        /*interrupted*/
                        if(!SEARCHER::chess_clock.pondering)
                            goto REDO1;
                    }
                    /*ponder with the move*/
                    if(move) {
                        mov_str(move,mv_str);
                        print_info("pondering after move [%s]\n",mv_str);
                        SEARCHER::expected_move = move;
                        SEARCHER::chess_clock.infinite_mode = true;
                        main_searcher->COPY(&searcher);
                        main_searcher->do_move(move);
                        move = main_searcher->find_best();
                        searcher.copy_root(main_searcher);

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
        this->copy_root(main_searcher);
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
        print("scorpio.ini not found!\n");
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
        searcher.copy_root(main_searcher);

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
