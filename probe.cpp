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

#define _NOTFOUND 99999
#define MAX_PIECES 9

typedef int (CDECL *PPROBE_EGBB) (int player, int* piece,int* square);
typedef void (CDECL *PLOAD_EGBB) (char* path,int cache_size,int load_options);
typedef void (CDECL *PLOAD_NN) (char* path);
typedef int (CDECL *PPROBE_NN) (int player, int* piece,int* square);
typedef int (CDECL *PADD_TO_BATCH) (int player, int* piece,int* square);
typedef int (CDECL *PPROBE_NN_BATCH) (int* evals);

static PPROBE_EGBB probe_egbb;
static PPROBE_NN probe_nn;
static PADD_TO_BATCH add_to_batch;
static PPROBE_NN_BATCH probe_nn_batch;

int SEARCHER::egbb_is_loaded = 0;
int SEARCHER::egbb_load_type = LOAD_4MEN;
int SEARCHER::egbb_depth_limit = 3;
int SEARCHER::egbb_ply_limit_percent = 75;
int SEARCHER::egbb_ply_limit;
int SEARCHER::egbb_cache_size = 16;
char SEARCHER::egbb_path[MAX_STR] = "egbb/";
char SEARCHER::nn_path[MAX_STR] = "nets/";
int SEARCHER::use_nn = 0;

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

int LoadEgbbLibrary(char* main_path,int egbb_cache_size) {
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
        add_to_batch = (PADD_TO_BATCH) GetProcAddress(hmod,"add_to_batch");
        probe_nn_batch = (PPROBE_NN_BATCH) GetProcAddress(hmod,"probe_neural_network_batch");
        load_egbb(main_path,egbb_cache_size,SEARCHER::egbb_load_type);
        if(load_nn) load_nn(SEARCHER::nn_path);
        else SEARCHER::use_nn = 0;
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

void SEARCHER::fill_list(int* piece, int* square) {
    PLIST current;
    int count = 0;

#define ADD_PIECE(list,type) {                  \
       current = list;                          \
       while(current) {                         \
          piece[count] = type;                  \
          square[count] = SQ8864(current->sq);  \
          current = current->next;              \
          count++;                              \
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
}

int SEARCHER::probe_bitbases(int& score) {
#ifdef EGBB
    int piece[MAX_PIECES],square[MAX_PIECES];
    fill_list(piece,square);
    score = probe_egbb(player,piece,square);
    if(score != _NOTFOUND)
        return true;
#endif
    return false;
}

int SEARCHER::probe_neural() {
#ifdef EGBB
    int piece[33],square[33];
    fill_list(piece,square);
    return probe_nn(player,piece,square);
#endif
    return 0;
}

void SEARCHER::add_to_batch_neural() {
#ifdef EGBB
    int piece[33],square[33];
    fill_list(piece,square);
    add_to_batch(player,piece,square);
#endif
}

void SEARCHER::probe_batch_neural(int* scores) {
#ifdef EGBB
    probe_nn_batch(scores);
#endif
}
/*
* EGBB cutoff
*/
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
