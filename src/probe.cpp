#include "scorpio.h"

#ifndef _WIN32   // Linux - Unix
#    define dlsym __shut_up
#    include <dlfcn.h>
#    undef dlsym
extern "C" void *(*dlsym(void *handle, const char *symbol))();
#endif

enum egbb_colors {
    _WHITE,_BLACK
};
enum egbb_occupancy {
    _EMPTY,_WKING,_WQUEEN,_WROOK,_WBISHOP,_WKNIGHT,_WPAWN,
    _BKING,_BQUEEN,_BROOK,_BBISHOP,_BKNIGHT,_BPAWN
};
enum egbb_load_types {
    LOAD_NONE,LOAD_4MEN,SMART_LOAD,LOAD_5MEN
};
enum {CPU, GPU};
enum {DEFAULT, LCZERO, SIMPLE};

#define _NOTFOUND 99999
#define MAX_PIECES 9

typedef int (CDECL *PPROBE_EGBB) (int player, int* piece, int* square);
typedef void (CDECL *PLOAD_EGBB) (char* path, int cache_size, int load_options);
typedef void (CDECL *PPROBE_NN) (float** iplanes, unsigned short** p_index, int* p_size,
                                 float** p_outputs, UBMP64 hash_key, bool hard_probe);
typedef void (CDECL *PLOAD_NN)(char* path, int nn_cache_size, int n_threads, 
                               int n_devices, int dev_type, int delay, int float_type, 
                               char* inps, char* outs, char* inp_shapes, char* output_sizes);
typedef void (CDECL *PSET_NUM_ACTIVE_SEARCHERS) (int n_searchers);

static PPROBE_EGBB probe_egbb;
static PPROBE_NN probe_nn;
static PSET_NUM_ACTIVE_SEARCHERS set_num_active_searchers = 0;

int SEARCHER::egbb_is_loaded = 0;
int SEARCHER::egbb_load_type = LOAD_4MEN;
int SEARCHER::egbb_depth_limit = 3;
int SEARCHER::egbb_ply_limit_percent = 75;
int SEARCHER::egbb_ply_limit;
int SEARCHER::egbb_cache_size = 16;
char SEARCHER::egbb_path[MAX_STR] = "egbb/";
char SEARCHER::nn_path[MAX_STR] = "nets/";
int SEARCHER::nn_cache_size = 16;
int SEARCHER::use_nn = 0;
int SEARCHER::save_use_nn = 0;
int SEARCHER::n_devices = 1;
int SEARCHER::device_type = CPU;
int SEARCHER::delay = 0;
int SEARCHER::float_type = 1;
int SEARCHER::nn_type = 0;
static bool is_trt = false;
static int CHANNELS = 24;
static int NPARAMS = 5;

/*
Load the dll and get the address of the load and probe functions.
*/

#ifdef _WIN32
#   ifdef ARC_64BIT
#       define EGBB_NAME "egbbdll64.dll"
#   else
#       define EGBB_NAME "egbbdll.dll"
#   endif
#else
#   ifdef ARC_64BIT
#       define EGBB_NAME "egbbso64.so"
#   else
#       define EGBB_NAME "egbbso.so"
#   endif
#endif

#ifndef _WIN32
#    define HMODULE void*
#    define LoadLibraryA(x) dlopen(x,RTLD_LAZY)
#    define FreeLibrary(x) dlclose(x)
#    define GetProcAddress dlsym
#endif

void init_index_table();
void init_input_planes();

int LoadEgbbLibrary(char* main_path,int egbb_cache_size,int nn_cache_size) {
#ifdef EGBB
    static HMODULE hmod = 0;
    PLOAD_EGBB load_egbb;
    PLOAD_NN load_nn;
    char path[256];
    size_t plen = strlen(main_path);
    if (plen) {
        char terminator = main_path[plen - 1];
        if (terminator != '/' && terminator != '\\') {
            if (strchr(main_path, '\\') != NULL)
                strcat(main_path, "\\");
            else
                strcat(main_path, "/");
        }
    }
    strcpy(path,main_path);
    strcat(path,EGBB_NAME);
    if(hmod) FreeLibrary(hmod);
    if((hmod = LoadLibraryA(path)) != 0) {
        load_egbb = (PLOAD_EGBB) GetProcAddress(hmod,"load_egbb_xmen");
        probe_egbb = (PPROBE_EGBB) GetProcAddress(hmod,"probe_egbb_xmen");
        load_nn = (PLOAD_NN) GetProcAddress(hmod,"load_neural_network");
        probe_nn = (PPROBE_NN) GetProcAddress(hmod,"probe_neural_network");
        set_num_active_searchers = (PSET_NUM_ACTIVE_SEARCHERS) GetProcAddress(hmod,"set_num_active_searchers");

        if(load_egbb)
            load_egbb(main_path,egbb_cache_size,SEARCHER::egbb_load_type);

        if(load_nn && SEARCHER::use_nn) {

            char input_names[256];
            char output_names[256];
            char input_shapes[256];
            char output_sizes[256];

            if(SEARCHER::nn_type == DEFAULT) {
                CHANNELS = 24;
                strcpy(input_names, "main_input aux_input");
                strcpy(input_shapes, "24 8 8  5 1 1");
                strcpy(output_names, "value/Softmax policy/Reshape");
                strcpy(output_sizes, "3 256");
            } else if(SEARCHER::nn_type == SIMPLE) {
                CHANNELS = 12;
                strcpy(input_names, "main_input aux_input");
                strcpy(input_shapes, "12 8 8");
                strcpy(output_names, "value/Softmax policy/Reshape");
                strcpy(output_sizes, "3 256");
            } else if(SEARCHER::nn_type == LCZERO) {
                CHANNELS = 112;
                strcpy(input_names, "main_input");
                strcpy(input_shapes, "112 8 8");
                strcpy(output_names, "value_head policy_head");
                strcpy(output_sizes, "1 256");
            }

            if(strstr(SEARCHER::nn_path, ".uff") != NULL)
                is_trt = true;
            else
                is_trt = false;

            init_index_table();
            init_input_planes();

            load_nn(SEARCHER::nn_path,nn_cache_size,PROCESSOR::n_processors,SEARCHER::n_devices,
                SEARCHER::device_type,SEARCHER::delay,SEARCHER::float_type, 
                input_names, output_names, input_shapes, output_sizes);
        } else
            SEARCHER::use_nn = 0;
        return true;
    } else {
        print("EgbbProbe not Loaded!\n");
    }
#endif
    return false;
}
/*
Probe:
Change interanal scorpio board representaion to [A1 = 0 ... H8 = 63]
board representation and then probe bitbase.
*/

void SEARCHER::fill_list(int& count, int* piece, int* square) {
    PLIST current;

#define ADD_PIECE(list,type) {                  \
       current = list;                          \
       while(current) {                         \
          piece[count] = type;                  \
          square[count] = SQ8864(current->sq);  \
          count++;                              \
          current = current->next;              \
       }                                        \
    };
    ADD_PIECE(plist[wking],_WKING);
    ADD_PIECE(plist[bking],_BKING);
    ADD_PIECE(plist[wqueen],_WQUEEN);
    ADD_PIECE(plist[bqueen],_BQUEEN);
    ADD_PIECE(plist[wrook],_WROOK);
    ADD_PIECE(plist[brook],_BROOK);
    ADD_PIECE(plist[wbishop],_WBISHOP);
    ADD_PIECE(plist[bbishop],_BBISHOP);
    ADD_PIECE(plist[wknight],_WKNIGHT);
    ADD_PIECE(plist[bknight],_BKNIGHT);
    ADD_PIECE(plist[wpawn],_WPAWN);
    ADD_PIECE(plist[bpawn],_BPAWN);
    piece[count] = _EMPTY;
    square[count] = SQ8864(epsquare);
    count++;
}

int SEARCHER::probe_bitbases(int& score) {
#ifdef EGBB
    int piece[MAX_PIECES],square[MAX_PIECES],count = 0;
    fill_list(count,piece,square);
    score = probe_egbb(player,piece,square);
    if(score != _NOTFOUND)
        return true;
#endif
    return false;
}

bool SEARCHER::bitbase_cutoff() {
#ifdef EGBB
    int score;
    int dlimit = egbb_depth_limit * UNITDEPTH;
    int plimit = (all_man_c <= MAX_EGBB) ? egbb_ply_limit : egbb_ply_limit / 2;
    if( egbb_is_loaded                                   //must be loaded
        && all_man_c <= MAX_EGBB                         //maximum 6 pieces
        && (pstack->depth >= dlimit || all_man_c <= 4 )  //hard depth limit
        && (ply >= plimit || fifty == 0)                 //ply above threshold or caps/pawn push
        && probe_bitbases(score)
        ) {
            egbb_probes++;

            /*prefer wins near root*/
            if(score > 0)
                score -= WIN_PLY * (ply + 1);
            else if(score < 0)
                score += WIN_PLY * (ply + 1);

            pstack->best_score = score;

            return true;
    }
#endif
    return false;
}

/*
Neural network
*/

static float* inp_planes[MAX_CPUS];
static unsigned short* all_pindex[MAX_CPUS];

void init_input_planes() {
    float* planes = 0;
    unsigned short* index = 0;
    const unsigned int N_PLANE = (8 * 8 * CHANNELS) + 
                ( (SEARCHER::nn_type == DEFAULT) ? NPARAMS : 0);

    aligned_reserve<float>(planes, PROCESSOR::n_processors * N_PLANE);
    aligned_reserve<unsigned short>(index, PROCESSOR::n_processors * MAX_MOVES);
    for(int i = 0; i < PROCESSOR::n_processors;i++) {
        inp_planes[i] = planes + i * N_PLANE;
        all_pindex[i] = index + i * MAX_MOVES;
    }
}

int SEARCHER::probe_neural(bool hard_probe) {
#ifdef EGBB
    UBMP64 hkey = ((player == white) ? hash_key : 
             (hash_key ^ UINT64(0x2bc3964f82352234)));

    unsigned short* const mindex = all_pindex[processor_id];
    for(int i = 0; i < pstack->count; i++) {
        MOVE& m = pstack->move_st[i];
        mindex[i] = compute_move_index(m, player);
    }

    nnecalls++;

    if(nn_type == DEFAULT || nn_type == SIMPLE) {

        int piece[33],square[33],isdraw[1];
        int count = 0, hist = 1;
        fill_list(count,piece,square);

        float* iplanes[2];
        iplanes[0] = inp_planes[processor_id];
        if(nn_type == DEFAULT)
            iplanes[1] = iplanes[0] + (8 * 8 * CHANNELS);
        fill_input_planes(player,castle,fifty,hist,
            isdraw,piece,square,iplanes[0],iplanes[1]);

        float wdl[3];
        unsigned short* p_index[2] = {0, mindex};
        int p_size[2] = {3, pstack->count};
        float* p_outputs[2] = {wdl,(float*)pstack->score_st};
        probe_nn(iplanes,p_index,p_size,p_outputs,hkey,hard_probe);
        float p = wdl[0] * 1.0 + wdl[1] * 0.5;
        return logit(p);

    } else {

        int piece[8*33],square[8*33],isdraw[8];
        int count = 0, hist = 0, phply = hply;
        
        for(int i = 0; i < 8; i++) {
            isdraw[hist++] = draw();
            fill_list(count,piece,square);

            if(hply > 0 && hstack[hply - 1].move) 
                POP_MOVE();
            else break;
        }

        count = phply - hply;
        for(int i = 0; i < count; i++)
            PUSH_MOVE(hstack[hply].move);

        float* iplanes[1];
        iplanes[0] = inp_planes[processor_id];
        fill_input_planes(player,castle,fifty,hist,
            isdraw,piece,square,iplanes[0],0);
        
        if(isdraw[0])
            hkey ^= UINT64(0xc7e9153edee38dcb);
        hkey ^= fifty_hkey[fifty];

        float wdl[1];
        unsigned short* p_index[2] = {0, mindex};
        int p_size[2] = {1, pstack->count};
        float* p_outputs[2] = {wdl,(float*)pstack->score_st};
        probe_nn(iplanes,p_index,p_size,p_outputs,hkey,hard_probe);
        float p = (wdl[0] + 1.0) * 0.5;
        return logit(p);
    }
#endif
    return 0;
}

void PROCESSOR::set_num_searchers() {
#ifdef EGBB
    if(SEARCHER::use_nn && set_num_active_searchers) {
        int n_searchers = n_processors - n_idle_processors;
        set_num_active_searchers(n_searchers);
    }
#endif
}

/*
Move policy format
*/

/* 1. AlphaZero format: 56=queen moves, 8=knight moves, 9 pawn promotions */
static const UBMP8 t_move_map[] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0, 35,  0,  0,  0,  0,  0,  0,
 27,  0,  0,  0,  0,  0,  0, 55,  0,  0, 36,  0,  0,  0,  0,  0,
 26,  0,  0,  0,  0,  0, 54,  0,  0,  0,  0, 37,  0,  0,  0,  0,
 25,  0,  0,  0,  0, 53,  0,  0,  0,  0,  0,  0, 38,  0,  0,  0,
 24,  0,  0,  0, 52,  0,  0,  0,  0,  0,  0,  0,  0, 39,  0,  0,
 23,  0,  0, 51,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 40, 60,
 22, 56, 50,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 61, 41,
 21, 49, 57,  0,  0,  0,  0,  0,  0,  7,  8,  9, 10, 11, 12, 13,
  0,  0,  1,  2,  3,  4,  5,  6,  0,  0,  0,  0,  0,  0, 63, 48,
 14, 28, 59,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 47, 62,
 15, 58, 29,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 46,  0,  0,
 16,  0,  0, 30,  0,  0,  0,  0,  0,  0,  0,  0, 45,  0,  0,  0,
 17,  0,  0,  0, 31,  0,  0,  0,  0,  0,  0, 44,  0,  0,  0,  0,
 18,  0,  0,  0,  0, 32,  0,  0,  0,  0, 43,  0,  0,  0,  0,  0,
 19,  0,  0,  0,  0,  0, 33,  0,  0, 42,  0,  0,  0,  0,  0,  0,
 20,  0,  0,  0,  0,  0,  0, 34,  0,  0,  0,  0,  0,  0,  0,  0
};
static const UBMP8* const move_map = t_move_map + 0x80;

/* 2. LcZero format: flat move representation */
static const int MOVE_TAB_SIZE = 64*64+8*3*3;

static unsigned short move_index_table[MOVE_TAB_SIZE];

void init_index_table() {

    memset(move_index_table, 0, MOVE_TAB_SIZE * sizeof(short));

    int cnt = 0;

    for(int from = 0; from < 64; from++) {

        int from88 = SQ6488(from);
        for(int to = 0; to < 64; to++) {
            int to88 = SQ6488(to);
            if(from != to) {
                if(sqatt_pieces(to88 - from88))
                    move_index_table[from * 64 + to] = cnt++;
            }
        }

    }

    for(int from = 48; from < 56; from++) {
        int idx = 4096 + file64(from) * 9;

        if(from > 48) {
            move_index_table[idx+0] = cnt++;
            move_index_table[idx+1] = cnt++;
            move_index_table[idx+2] = cnt++;
        }

        move_index_table[idx+3] = cnt++;
        move_index_table[idx+4] = cnt++;
        move_index_table[idx+5] = cnt++;

        if(from < 55) {
            move_index_table[idx+6] = cnt++;
            move_index_table[idx+7] = cnt++;
            move_index_table[idx+8] = cnt++;
        }
    }
}

int compute_move_index(MOVE& m, int player) {

    int from = m_from(m), to = m_to(m), prom, index;
    if(is_castle(m)) {
        if(to > from) to++;
        else to -= 2;
    }
    from = SQ8864(from);
    to = SQ8864(to); 
    prom = m_promote(m); 

    if(player == black) {
        from = MIRRORR64(from);
        to = MIRRORR64(to);
    }

    if(SEARCHER::nn_type == DEFAULT || SEARCHER::nn_type == SIMPLE) {
        index = from * 73;
        if(prom) {
            prom = PIECE(prom);
            if(prom != queen)
                index += 64 + (to - from - 7) * 3  + (prom - queen);
            else
                index += move_map[SQ6488(to) - SQ6488(from)];
        } else {
            index += move_map[SQ6488(to) - SQ6488(from)];
        }
    } else {
        int compi = from * 64 + to;
        if(prom) {
            prom = PIECE(prom);
            if(prom != knight) {
                compi = 4096 +  file64(from) * 9 + 
                        (to - from - 7) * 3 + (prom - queen);
            }
        }

        index = move_index_table[compi];
    }

    return index;
}

/*
   Fill input planes
*/
#define invert_color(x)  (((x) > 6) ? ((x) - 6) : ((x) + 6))

void fill_input_planes(
    int player, int cast, int fifty, int hist, int* draw, 
    int* piece, int* square, float* data, float* adata
    ) {
    
    int pc, col, sq, to;

    /* 
       Add the attack map planes 
    */
#define DHWC(sq,C)     data[rank(sq) * 8 * CHANNELS + file(sq) * CHANNELS + C]
#define DCHW(sq,C)     data[C * 8 * 8 + rank(sq) * 8 + file(sq)]
#define D(sq,C)        ( (is_trt || (SEARCHER::nn_type > DEFAULT) ) ? DCHW(sq,C) : DHWC(sq,C) )

#define NK_MOVES(dir, off) {                    \
        to = sq + dir;                          \
        if(!(to & 0x88)) D(to, off) = 1.0f;     \
}

#define BRQ_MOVES(dir, off) {                   \
        to = sq + dir;                          \
        while(!(to & 0x88)) {                   \
            D(to, off) = 1.0f;                  \
            if(board[to] != 0) break;           \
                to += dir;                      \
        }                                       \
}

    memset(data,  0, sizeof(float) * 8 * 8 * CHANNELS);

    if(SEARCHER::nn_type == DEFAULT) {

        int board[128];

        memset(board, 0, sizeof(int) * 128);
        memset(adata, 0, sizeof(float) * NPARAMS);

        for(int i = 0; (pc = piece[i]) != _EMPTY; i++) {
            sq = SQ6488(square[i]);
            if(player == _BLACK) {
                sq = MIRRORR(sq);
                pc = invert_color(pc);
            }

            board[sq] = pc;
        }

        for(int i = 0; (pc = piece[i]) != _EMPTY; i++) {
            sq = SQ6488(square[i]);
            if(player == _BLACK) {
                sq = MIRRORR(sq);
                pc = invert_color(pc);
            }
            D(sq,(pc+11)) = 1.0f;
            switch(pc) {
                case wking:
                    NK_MOVES(RU,0);
                    NK_MOVES(LD,0);
                    NK_MOVES(LU,0);
                    NK_MOVES(RD,0);
                    NK_MOVES(UU,0);
                    NK_MOVES(DD,0);
                    NK_MOVES(RR,0);
                    NK_MOVES(LL,0);
                    break;
                case wqueen:
                    BRQ_MOVES(RU,1);
                    BRQ_MOVES(LD,1);
                    BRQ_MOVES(LU,1);
                    BRQ_MOVES(RD,1);
                    BRQ_MOVES(UU,1);
                    BRQ_MOVES(DD,1);
                    BRQ_MOVES(RR,1);
                    BRQ_MOVES(LL,1);
                    break;
                case wrook:
                    BRQ_MOVES(UU,2);
                    BRQ_MOVES(DD,2);
                    BRQ_MOVES(RR,2);
                    BRQ_MOVES(LL,2);
                    break;
                case wbishop:
                    BRQ_MOVES(RU,3);
                    BRQ_MOVES(LD,3);
                    BRQ_MOVES(LU,3);
                    BRQ_MOVES(RD,3);
                    break;
                case wknight:
                    NK_MOVES(RRU,4);
                    NK_MOVES(LLD,4);
                    NK_MOVES(RUU,4);
                    NK_MOVES(LDD,4);
                    NK_MOVES(LLU,4);
                    NK_MOVES(RRD,4);
                    NK_MOVES(RDD,4);
                    NK_MOVES(LUU,4);
                    break;
                case wpawn:
                    NK_MOVES(RU,5);
                    NK_MOVES(LU,5);
                    break;
                case bking:
                    NK_MOVES(RU,6);
                    NK_MOVES(LD,6);
                    NK_MOVES(LU,6);
                    NK_MOVES(RD,6);
                    NK_MOVES(UU,6);
                    NK_MOVES(DD,6);
                    NK_MOVES(RR,6);
                    NK_MOVES(LL,6);
                    break;
                case bqueen:
                    BRQ_MOVES(RU,7);
                    BRQ_MOVES(LD,7);
                    BRQ_MOVES(LU,7);
                    BRQ_MOVES(RD,7);
                    BRQ_MOVES(UU,7);
                    BRQ_MOVES(DD,7);
                    BRQ_MOVES(RR,7);
                    BRQ_MOVES(LL,7);
                    break;
                case brook:
                    BRQ_MOVES(UU,8);
                    BRQ_MOVES(DD,8);
                    BRQ_MOVES(RR,8);
                    BRQ_MOVES(LL,8);
                    break;
                case bbishop:
                    BRQ_MOVES(RU,9);
                    BRQ_MOVES(LD,9);
                    BRQ_MOVES(LU,9);
                    BRQ_MOVES(RD,9);
                    break;
                case bknight:
                    NK_MOVES(RRU,10);
                    NK_MOVES(LLD,10);
                    NK_MOVES(RUU,10);
                    NK_MOVES(LDD,10);
                    NK_MOVES(LLU,10);
                    NK_MOVES(RRD,10);
                    NK_MOVES(RDD,10);
                    NK_MOVES(LUU,10);
                    break;
                case bpawn:
                    NK_MOVES(RD,11);
                    NK_MOVES(LD,11);
                    break;
            }

            col = PCOLOR(pc);
            pc = PIECE(pc);

            if(pc != king) {
                if(col == white)
                    adata[pc - queen]++;
                else
                    adata[pc - queen]--;
            }
        }
    } else if (SEARCHER::nn_type == SIMPLE) {
        for(int i = 0; (pc = piece[i]) != _EMPTY; i++) {
            sq = SQ6488(square[i]);
            if(player == _BLACK) {
                sq = MIRRORR(sq);
                pc = invert_color(pc);
            }
            D(sq,(pc-1)) = 1.0f;
        }
    } else {

        static const int piece_map[2][12] = {
            {
                wpawn,wknight,wbishop,wrook,wqueen,wking,
                bpawn,bknight,bbishop,brook,bqueen,bking
            },
            {
                bpawn,bknight,bbishop,brook,bqueen,bking,
                wpawn,wknight,wbishop,wrook,wqueen,wking
            }
        };

        for(int h = 0, i = 0; h < hist; h++) {
            for(; (pc = piece[i]) != _EMPTY; i++) {
                sq = SQ6488(square[i]);
                if(player == _BLACK) 
                    sq = MIRRORR(sq);
                int off = piece_map[player][pc - wking]
                         - wking + 13 * h;
                D(sq,off) = 1.0f;
            }
            if(draw && draw[h]) {
                int off = 13 * h + 12;
                for(int i = 0; i < 64; i++) {
                    sq = SQ6488(i);
                    D(sq,off) = 1.0;
                }
            }
            i++;
        }

        for(int i = 0; i < 64; i++) {
            sq = SQ6488(i);
            if(player == _BLACK) {
                if(cast & 8) D(sq,(CHANNELS - 8)) = 1.0;
                if(cast & 4) D(sq,(CHANNELS - 7)) = 1.0;
                if(cast & 2) D(sq,(CHANNELS - 6)) = 1.0;
                if(cast & 1) D(sq,(CHANNELS - 5)) = 1.0;
                D(sq,(CHANNELS - 4)) = 1.0;
            } else {
                if(cast & 2) D(sq,(CHANNELS - 8)) = 1.0;
                if(cast & 1) D(sq,(CHANNELS - 7)) = 1.0;
                if(cast & 8) D(sq,(CHANNELS - 6)) = 1.0;
                if(cast & 4) D(sq,(CHANNELS - 5)) = 1.0;
                D(sq,(CHANNELS - 4)) = 0.0;
            }
            D(sq,(CHANNELS - 3)) = fifty / 100.0;
            D(sq,(CHANNELS - 1)) = 1.0;
        }
    }

#if 0
        for(int c = 0; c < CHANNELS;c++) {
            printf("Channel %d\n",c);
            for(int i = 0; i < 8; i++) {
                for(int j = 0; j < 8; j++) {
                    int sq = SQ(i,j);
                    printf("%d ",int(D(sq,c)));
                }
                printf("\n");
            }
            printf("\n");
        }
        fflush(stdout);
#endif

#undef NK_MOVES
#undef BRQ_MOVES
#undef D
}

/*
Write input planes to file
*/
void SEARCHER::write_input_planes(FILE* file) {

    float* iplanes[2] = {0, 0};

    if(nn_type == DEFAULT || nn_type == SIMPLE) {

        int piece[33],square[33],isdraw[1];
        int count = 0, hist = 1;
        fill_list(count,piece,square);

        
        iplanes[0] = inp_planes[processor_id];
        if(nn_type == DEFAULT)
            iplanes[1] = iplanes[0] + (8 * 8 * CHANNELS);
        fill_input_planes(player,castle,fifty,hist,
            isdraw,piece,square,iplanes[0],iplanes[1]);

    } else {

        int piece[8*33],square[8*33],isdraw[8];
        int count = 0, hist = 0, phply = hply;
        
        for(int i = 0; i < 8; i++) {
            isdraw[hist++] = draw();
            fill_list(count,piece,square);

            if(hply > 0 && hstack[hply - 1].move) 
                POP_MOVE();
            else break;
        }

        count = phply - hply;
        for(int i = 0; i < count; i++)
            PUSH_MOVE(hstack[hply].move);

        iplanes[0] = inp_planes[processor_id];
        fill_input_planes(player,castle,fifty,hist,
            isdraw,piece,square,iplanes[0],0);
    }

    const int NPLANE = 8 * 8 * CHANNELS;
    fwrite(iplanes[0], NPLANE * sizeof(float), 1, file);
    if(nn_type == DEFAULT)
        fwrite(iplanes[1], NPARAMS * sizeof(float), 1, file);
}