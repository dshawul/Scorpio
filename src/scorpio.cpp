#include "scorpio.h"

#define VERSION "3.0.14d"

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

CACHE_ALIGN int16_t pcsq[14][0x80];
bool book_loaded = false;
bool log_on = false;
int scorpio_start_time;
bool is_selfplay = false;
int  PROTOCOL = CONSOLE;
int variant = 0;

/*
parallel search
*/
PPROCESSOR processors[MAX_CPUS] = {0};
int PROCESSOR::n_processors;
int PROCESSOR::n_cores;
std::atomic_int PROCESSOR::n_idle_processors;
int PROCESSOR::n_hosts = 1;

LOCK  lock_smp;
LOCK  lock_io;
int PROCESSOR::SMP_SPLIT_DEPTH = 4;
int use_abdada_smp = 0;

#ifdef CLUSTER
std::atomic_int PROCESSOR::message_available = {0};
int PROCESSOR::CLUSTER_SPLIT_DEPTH = 8;
int PROCESSOR::host_id;
char PROCESSOR::host_name[256];
std::list<int> PROCESSOR::available_host_workers;
std::vector<PV_MESSAGE> PROCESSOR::node_pvs;
std::vector<Node*> PROCESSOR::pv_tree_nodes;
int PROCESSOR::help_messages = 0;
int PROCESSOR::prev_dest = -1;
int PROCESSOR::vote_weight = 100;
const char *const PROCESSOR::message_str[] = {
    "QUIT","INIT","HELP","CANCEL","SPLIT","MERGE","PING","PONG",
    "PV","STRING","STRING_CMD","GOROOT","RECORD_TT","PROBE_TT","PROBE_TT_RESULT"
};
int use_abdada_cluster = 0;
#endif
/*
static global variables of SEARCHER
*/
uint64_t SEARCHER::root_nodes[MAX_MOVES];
int SEARCHER::root_scores[MAX_MOVES];
CACHE_ALIGN int16_t SEARCHER::history[14][64];
CACHE_ALIGN int16_t SEARCHER::history_ply[64][6][64];
CACHE_ALIGN MOVE SEARCHER::refutation[14][64];
int16_t* SEARCHER::ref_fup_history = 0;
CHESS_CLOCK SEARCHER::chess_clock;
std::atomic_uint SEARCHER::playouts;
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
unsigned int SEARCHER::root_node_reuse_visits = 0;
bool SEARCHER::has_ab = false;
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
static int mt = 1;

static bool load_ini();
static void init_game();
static int self_play();

/*
load egbbs with a separate thread
*/
static bool egbb_setting_changed = false;
static bool ht_setting_changed = false;

static void load_egbbs(bool send = true) {
    if(send) {
        CLUSTER_CODE(if(PROCESSOR::host_id == 0) PROCESSOR::send_cmd("load_egbbs");)
    }
    /*reset hash tables*/
    if(ht_setting_changed) {
        uint64_t size, size_max;
        int np = (montecarlo && SEARCHER::use_nn) ? PROCESSOR::n_cores : mt;

        char header[512], header2[128];

#ifdef CLUSTER
        sprintf(header,"-------- Host %d ----------\n",PROCESSOR::host_id);
#else
        strcpy(header,"--------------------------\n");
#endif
        if(!(montecarlo && frac_abprior == 0)) {
            size = 1;
            size_max = ht * ((1024 * 1024) / (2 * sizeof(HASH)));
            while(2 * size <= size_max) size *= 2;
            PROCESSOR::hash_tab_mask = size - 1;
            sprintf(header2,"ht %lu X 2 X %d X %d = %.1f MB\n",(unsigned long)size,(int)sizeof(HASH), 1,
                (2 * size * sizeof(HASH)) / double(1024 * 1024));
            strcat(header,header2);

            size = 1;
            size_max = eht * ((1024 * 1024) / (2 * sizeof(EVALHASH)));
            while(2 * size <= size_max) size *= 2;
            PROCESSOR::eval_hash_tab_mask = size - 1;
            sprintf(header2,"eht %u X 2 X %d X %d = %.1f MB\n",(unsigned int)size,(int)sizeof(EVALHASH), np,
                np * (2 * size * sizeof(EVALHASH)) / double(1024 * 1024));
            strcat(header,header2);
        }
        if(!SEARCHER::use_nnue) {
            size = 1;
            size_max = pht * ((1024 * 1024) / (sizeof(PAWNHASH)));
            while(2 * size <= size_max) size *= 2;
            PROCESSOR::pawn_hash_tab_mask = size - 1;
            sprintf(header2,"pht %u X 1 X %d X %d = %.1f MB\n",(unsigned int)size,(int)sizeof(PAWNHASH), np,
                np * (size * sizeof(PAWNHASH)) / double(1024 * 1024));
            strcat(header,header2);
        }

        init_smp(mt);
        sprintf(header2,"processors [%d]\n",PROCESSOR::n_processors);
        strcat(header,header2);
        int core = 0, node = 0;
        get_core_node(core,node);
        sprintf(header2, "Process running on node %d and core %d\n",node,core);
        strcat(header,header2);
        strcat(header,"--------------------------\n");
        print_cluster(header);

        ht_setting_changed = false;
    }
    /*Wait if we are still loading EGBBs*/
    if(egbb_setting_changed && !SEARCHER::egbb_is_loaded) {
        egbb_setting_changed = false;
        int start = get_time();
        LoadEgbbLibrary(SEARCHER::egbb_path);
        int end = get_time();
        print("loading_time = %ds\n",(end - start) / 1000);
    }

    /*sync*/
    CLUSTER_CODE(PROCESSOR::Barrier();)
}
/*
main
*/
int CDECL main(int argc, char* argv[]) {
    char   buffer[4*MAX_FILE_STR];
    char*  commands[4*MAX_STR];

    /*init mpi*/
#ifdef CLUSTER
    PROCESSOR::init_mpi(argc,argv);
#endif

    /*init io*/
    init_io();

    /*init mpi message processing thread*/
#ifdef CLUSTER
    PROCESSOR::init_mpi_thread();
#endif
    
    /*init game*/
    init_game();

    /*
     * Parse scorpio.ini file which contains settings of scorpio. 
     * Search/eval execution commands should not be put there.
     */
    load_ini();

    /*
     * Add all host workers to list of helpers
     */
#ifdef CLUSTER
    if(use_abdada_cluster && (PROCESSOR::host_id == 0)) {
        for(int i = 1; i < PROCESSOR::n_hosts; i++)
            PROCESSOR::available_host_workers.push_back(i);
    }
#endif

    /*
     * Parse commands from the command line
     */
    strcpy(buffer,"");
    for(int i = 1;i < argc;i++) {
#ifdef CLUSTER
        if(PROCESSOR::host_id) {
            if(!strcmp(argv[i],"xboard") || !strcmp(argv[i],"uci"))
                continue;
        }
#endif
        strcat(buffer," ");
        strcat(buffer,argv[i]);
    }
    print_log("<COMMAND LINE>%s\n",buffer);

    commands[tokenize(buffer,commands)] = NULL;
    if(!parse_commands(commands))
        goto END;

    /* remove log file if not set*/
    if(!log_on)
        remove_log_file();

    /*
     * Host 0 processes command line
     */
#ifdef CLUSTER
    if(PROCESSOR::host_id == 0) {
#endif
        /*
         * Parse commands from stdin.
         */
        print_log("==============================\n");
        while(true) {
            if(!read_line(buffer))
                goto END;
            if(PROTOCOL == CONSOLE || PROTOCOL == UCI) {
                std::string s(buffer);
                size_t pos;

                while((pos = s.find("setoption name")) != std::string::npos)
                    s.replace(pos, 14, " ");
                while((pos = s.find("value")) != std::string::npos)
                    s.replace(pos, 5, " ");

                strcpy(buffer,s.c_str());
            }
            commands[tokenize(buffer,commands)] = NULL;
            if(!parse_commands(commands))
                goto END;
        }
#ifdef  CLUSTER
    } else {
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
    l_create(lock_smp);
    scorpio_start_time = get_time();
    PROCESSOR::n_cores = 1;
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
    SEARCHER::allocate_history();
    load_book();
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
    "mt/cores   <N>      -- Set the number of parallel threads of execution to N.",
    "          auto      -- Set to available logical cores.",
    "          auto-<R>  -- Set to (logical cores - R).",
    "          auto/<R>  -- Set to (logical cores / R).",
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
    "score -- score runs the evaluation function on the current position.",
    "sd -- sd <DEPTH> {The engine should limit its thinking to <DEPTH> ply.}",
    "setboard -- setboard <FEN> is used to set up FEN position <FEN>.",
    "st -- st <TIME> {Set time controls search time in seconds.}",
    "time -- time <N> {Set a clock that belongs to the engine in centiseconds.}",
    "param_group -- param_group <N> sets parameter group to tune",
    "zero_params -- zeros all evaluation parameters",
    "tune -- tune Tune evaluation function.",
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
    static const char* sched[] = {"FCFS", "ROUNDROBIN"};
    print_check("log",log_on);
    print_button("clear_hash");
    print_spin("resign",SEARCHER::resign_value,100,30000);
    print_spin("mt",PROCESSOR::n_processors,1,MAX_CPUS);
    print_spin("ht",ht,1,131072);
    print_spin("eht",eht,1,16384);
    print_spin("pht",pht,1,256);
    print_path("egbb_path",SEARCHER::egbb_path);
    print_path("egbb_files_path",SEARCHER::egbb_files_path);
    print_check("use_nn",SEARCHER::use_nn);
    print_check("use_nnue",SEARCHER::use_nnue);
    print_path("nn_path",SEARCHER::nn_path);
    print_path("nn_path_e",SEARCHER::nn_path_e);
    print_path("nn_path_m",SEARCHER::nn_path_m);
    print_path("nnue_path",SEARCHER::nnue_path);
    print_spin("egbb_cache_size",SEARCHER::egbb_cache_size,1,16384);
    print_spin("egbb_load_type",SEARCHER::egbb_load_type,0,3);
    print_spin("egbb_depth_limit",SEARCHER::egbb_depth_limit,0,MAX_PLY);
    print_spin("egbb_ply_limit_percent",SEARCHER::egbb_ply_limit_percent,0,100);
    print_spin("nn_cache_size",SEARCHER::nn_cache_size,1,16384);
    print_spin("nn_cache_size_m",SEARCHER::nn_cache_size_m,1,16384);
    print_spin("nn_cache_size_e",SEARCHER::nn_cache_size_e,1,16384);
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
    print_spin("nnue_scale",SEARCHER::nnue_scale,0,1024);
    print_spin("nnue_type",SEARCHER::nnue_type,0,1);
    print_spin("batch_size_factor",SEARCHER::batch_size_factor,0,12);
    print_combo("scheduling",sched,SEARCHER::batch_size_factor,2);
    CLUSTER_CODE(print_spin("vote_weight",PROCESSOR::vote_weight,0,1000));
}
/**
* Internal scorpio commands
*/
int internal_commands(char** commands,char* command,int& command_num) {
    if (!strcmp(command, "xboard")) {
        PROTOCOL = XBOARD;
        print("feature done=0\n");
        print("feature name=1 myname=\"Scorpio %s\"\n",VERSION);
        print("feature sigint=0 sigterm=0\n");
        print("feature variants=\"normal,fischerandom\"\n");
        print("feature setboard=1 usermove=1 draw=0 colors=0\n");
        print("feature smp=0 memory=0 debug=1\n");
        print_options();
        print_search_params();
        print_mcts_params();
#ifdef TUNE
        print_eval_params();
#endif
        /*load egbbs*/
        CLUSTER_CODE(if(PROCESSOR::host_id == 0) PROCESSOR::send_cmd(command);)
        load_egbbs(false);

        print("feature done=1\n");
    } else if(!strcmp(command,"uci")) {
        PROTOCOL = UCI;
        CLUSTER_CODE(if(PROCESSOR::host_id == 0))
        {
            print("id name Scorpio %s\n",VERSION);
            print("id author Daniel Shawul\n");
            print_check("UCI_Chess960",variant);
            print_options();
            print_search_params();
            print_mcts_params();
#ifdef TUNE
            print_eval_params();
#endif
            print("uciok\n");
        }
        /*load egbbs*/
        CLUSTER_CODE(if(PROCESSOR::host_id == 0) PROCESSOR::send_cmd(command);)
        load_egbbs(false);
        /*
        hash tables
        */
    } else if(!strcmp(command,"load_egbbs")) {
        load_egbbs(false);
    } else if(!strcmp(command,"ht")) {
        ht = atoi(commands[command_num++]);
        ht_setting_changed = true;
    } else if(!strcmp(command,"pht")) {
        pht = atoi(commands[command_num++]);
        ht_setting_changed = true;
    } else if(!strcmp(command,"eht")) {
        eht = atoi(commands[command_num++]);
        ht_setting_changed = true;
        /*
        parallel search
        */
    } else if(!strcmp(command,"affinity")) {
        int affinity = atoi(commands[command_num]);
        affinity = MIN(affinity, MAX_CPUS);
        PROCESSOR::n_cores = set_affinity(affinity);
        command_num++;
    } else if(!strcmp(command,"mt") || !strcmp(command,"cores") || !strcmp(command,"Threads") ) {
        if(strcmp(command,"mt") && montecarlo && SEARCHER::use_nn);
        else {
            if(!strcmp(commands[command_num],"auto"))
                mt = PROCESSOR::n_cores;
            else if(!strncmp(commands[command_num],"auto-",5)) {
                int r = atoi(&commands[command_num][5]);
                mt = PROCESSOR::n_cores - r;
            } else if(!strncmp(commands[command_num],"auto/",5)) {
                int r = atoi(&commands[command_num][5]);
                mt = PROCESSOR::n_cores / r;
            } else
                mt = atoi(commands[command_num]);
            mt = MIN(mt, MAX_CPUS);
            ht_setting_changed = true;
        }
        command_num++;
#ifdef CLUSTER
    } else if(!strcmp(command, "vote_weight")) {
        PROCESSOR::vote_weight = atoi(commands[command_num]);
        command_num++;
#endif
        /*
        egbb
        */
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
    } else if(!strcmp(command, "nn_cache_size_m")) {
        egbb_setting_changed = true;
        SEARCHER::nn_cache_size_m = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nn_cache_size_e")) {
        egbb_setting_changed = true;
        SEARCHER::nn_cache_size_e = atoi(commands[command_num]);
        command_num++;
    } else if (!strcmp(command, "use_nn")) {
        SEARCHER::use_nn = is_checked(commands[command_num]);
        SEARCHER::save_use_nn = SEARCHER::use_nn;
        command_num++;
    } else if (!strcmp(command, "use_nnue")) {
        SEARCHER::use_nnue = is_checked(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nnue_scale")) {
        SEARCHER::nnue_scale = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "nnue_type")) {
        SEARCHER::nnue_type = atoi(commands[command_num]);
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
    } else if(!strcmp(command, "nnue_path")) {
        strcpy(SEARCHER::nnue_path,commands[command_num]);
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
    } else if(!strcmp(command, "batch_size_factor")) {
        SEARCHER::batch_size_factor = atoi(commands[command_num]);
        command_num++;
    } else if(!strcmp(command, "scheduling")) {
        command = commands[command_num++];
        if(!strcmp(command,"FCFS")) SEARCHER::scheduling = 0;
        else SEARCHER::scheduling = 1;

    } else if (!strcmp(command, "book")) {
        if(commands[command_num]) {
            book_loaded = is_checked(commands[command_num]);
            command_num++;
        } else if(book_loaded) {
            searcher.show_book_moves();
        }
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
    } else if (!strcmp(command,"pgn_to_epd") ||
               !strcmp(command,"pgn_to_score") ||
               !strcmp(command,"pgn_to_nn") ||
               !strcmp(command,"pgn_to_dat") ||
               !strcmp(command,"epd_to_score") ||
               !strcmp(command,"epd_to_nn") ||
               !strcmp(command,"epd_to_dat") ||
               !strcmp(command,"epd_to_quiet") ||
               !strcmp(command,"epd_check_eval") ||
               !strcmp(command,"epd_run_search") ||
               !strcmp(command,"epd_run_perft")
        ) {
#ifdef CLUSTER
        if(PROCESSOR::host_id != 0)
            return 2;
#endif
        load_egbbs();

        /*task number*/
        int task;
        if(!strcmp(command,"pgn_to_epd"))
            task = 0;
        else if(!strcmp(command,"pgn_to_score"))
            task = 1;
        else if(!strcmp(command,"pgn_to_nn"))
            task = 2;
        else if(!strcmp(command,"pgn_to_dat"))
            task = 3;
        else if(!strcmp(command,"epd_to_score"))
            task = 4;
        else if(!strcmp(command,"epd_to_nn"))
            task = 5;
        else if(!strcmp(command,"epd_to_dat"))
            task = 6;
        else if(!strcmp(command,"epd_to_quiet"))
            task = 7;
        else if(!strcmp(command,"epd_check_eval"))
            task = 8;
        else if(!strcmp(command,"epd_run_search"))
            task = 9;
        else
            task = 10;

        char source[1024],dest[1024];
        strcpy(source,commands[command_num++]);
        if(task < 8)
            strcpy(dest,commands[command_num++]);

        /*header*/
        if(task == 9) {
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

        /*process pgn/epd*/
        FILE* fb = 0;
        if(task == 3 || task == 6)
            fb = fopen(dest,"wb");
        else if(task < 8)
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
            main_searcher->worker_thread_all(&epd,fb,task,(task == 9));
            epd.close();
        }

        if(task < 8)
            fclose(fb);
#ifdef TUNE
    } else if (!strcmp(command, "jacobian") ||
               !strcmp(command, "tune")
               ) {
        load_egbbs();

        /*task number*/
        int task;
        if(!strcmp(command,"jacobian"))
            task = 0;
        else
            task = 1;

        /*call tuner*/
        searcher.tune(task,commands[command_num++]);
#endif
    } else if (!strcmp(command, "log")) {
        if(is_checked(commands[command_num]))
            log_on = true;
        else
            log_on = false;
        command_num++;
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
        uint64_t nodes = searcher.perft(depth);
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
    } else if(!strcmp(command,"randomize")) {
        is_selfplay = true;
    } else if(!strcmp(command,"quit")) {
#ifdef CLUSTER
        if(PROCESSOR::host_id != 0)
            return 2;
#endif
        print("Bye Bye\n");
        return 3;
    } else if (!strcmp(command, "variant")) {
        if(!strcmp(commands[command_num++], "fischerandom"))
            variant = 1;
        else
            variant = 0;
#ifdef CLUSTER
        if(PROCESSOR::host_id == 0 && variant)
            PROCESSOR::send_cmd("variant fischerandom");
#endif
    } else if(!strcmp(command,"selfplay")) {
        int wins = 0, losses = 0, draws = 0,res;
        char FEN[MAX_STR];
        int N = atoi(commands[command_num++]);
        char name[512];
        int unique_id = GETPID() + rand();
        sprintf(name,"%s.%d",commands[command_num++],unique_id);
        FILE* fw = fopen(name,"w");

        load_egbbs();

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
        
        print("Starting %d selfplay games\n",N);
        is_selfplay = true;
        main_searcher->COPY(&searcher);
        main_searcher->self_play_thread_all(fw,fw2,N);

        print("Finished\n");
        fclose(fw);
        fclose(fw2);
    } else {
        return 0;
    }
    return 1;
}
/**
 * xboard protocol
 */
int xboard_commands(char** commands,char* command,int& command_num,int& do_search) {
    if (!strcmp(command, "protover")) {
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
            float mn,sec;
            sscanf(commands[command_num],"%f:%f",&mn,&sec);
            SEARCHER::chess_clock.p_time = 60000 * mn + 1000 * sec;
            command_num++;
        } else {
            SEARCHER::chess_clock.p_time = 60000 * atof(commands[command_num++]);
        }
        SEARCHER::chess_clock.p_inc = 1000 * atof(commands[command_num++]);
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
        SEARCHER::first_search = true;
        SEARCHER::old_root_score = 0;
        SEARCHER::root_score = 0;
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
        searcher.str_mov(move,command);
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
        print("readyok\n");
    } else if (!strcmp(command, "UCI_Chess960")) {
        variant = is_checked(commands[command_num++]);
#ifdef CLUSTER
        if(PROCESSOR::host_id == 0 && variant)
            PROCESSOR::send_cmd("UCI_Chess960 true");
#endif
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
                searcher.str_mov(move,command);
                if(searcher.is_legal(move)) {
                    searcher.do_move(move);
                } else {
                    print("Illegal move: %s\n", command);
                }
            }
        } else {
            SEARCHER::first_search = true;
            SEARCHER::old_root_score = 0;
            SEARCHER::root_score = 0;
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
    main_searcher->mov_str(move,mv_str);
    print_log("<%012d>",get_time() - scorpio_start_time);
    if(PROTOCOL == UCI) {
        if(SEARCHER::expected_move) {
            char mv_ponder_str[10];
            MOVE move_ponder = SEARCHER::expected_move;
            main_searcher->mov_str(move_ponder,mv_ponder_str);
            print("bestmove %s ponder %s\n",mv_str, mv_ponder_str);
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
        int ret = internal_commands(commands,command,command_num);
        if(ret == 3) {
            return false;
        } else if(!ret) {
            ret = 0;
            if(PROTOCOL == XBOARD || PROTOCOL == CONSOLE) 
                ret = xboard_commands(commands,command,command_num,do_search);
            else if(PROTOCOL == UCI)
                ret = uci_commands(commands,command,command_num,do_search);
            if(ret == 1) return true;
            else if(ret == 0) return false;
        }

        /*skip "go" instruction for all other hosts than 0*/
#ifdef CLUSTER
        if(PROCESSOR::host_id != 0)
            continue;
#endif
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
                        main_searcher->mov_str(move,mv_str);
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
