#ifndef __SCORPIO__
#define __SCORPIO__

/*Disable some MSVC warnings*/
#ifdef _MSC_VER
#    pragma warning (disable: 4305)
#    pragma warning (disable: 4267)
#    pragma warning (disable: 4146)
#    pragma warning (disable: 4244)
#endif

/*
Some definitions to include/remove code
*/
#ifdef _MSC_VER
// #    define CLUSTER
// #    define TUNE
#endif

/*includes*/
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cctype>
#include <ctime>
#include <cmath>
#include <string>
#ifdef _WIN32
#   include <sys/timeb.h>
#else
#   include <sys/time.h>
#endif
#include <vector>
#ifdef CLUSTER
#  include <list>
#  include "mpi.h"
#endif
#include "my_types.h"

/*
parallel search options
*/
#if !defined(MAX_CPUS)
#   define MAX_CPUS             656
#endif
#if defined(YBW)
#   define MAX_SEARCHERS_PER_CPU 32
#   define MAX_CPUS_PER_SPLIT     8
#else
#   define MAX_SEARCHERS_PER_CPU  1
#   define MAX_CPUS_PER_SPLIT     1
#endif
/*
* Transposition table type
* 0 - global tt
* 1 - distributed tt
* 2 - local tt
*/
#if !defined(CLUSTER_TT_TYPE)
#   define CLUSTER_TT_TYPE           2
#endif

/*typedefs*/
typedef uint64_t  HASHKEY;
typedef uint32_t  MOVE;

/*
Scorpio board representation
*/
enum colors {
    white,black,neutral
};
enum chessmen {
    king = 1,queen,rook,bishop,knight,pawn
};
#undef blank
enum occupancy {
    blank,wking,wqueen,wrook,wbishop,wknight,wpawn,
    bking,bqueen,brook,bbishop,bknight,bpawn,elephant
};
enum ranks {
    RANK1,RANK2,RANK3,RANK4,RANK5,RANK6,RANK7,RANK8
};
enum files {
    FILEA,FILEB,FILEC,FILED,FILEE,FILEF,FILEG,FILEH
};
enum hash_types {
    UNKNOWN,AVOID_NULL,HASH_HIT,HASH_GOOD,EXACT,UPPER,LOWER,CRAP
};
enum results {
    R_UNKNOWN,R_WWIN,R_BWIN,R_DRAW
};
enum node_types {
    PV_NODE,CUT_NODE,ALL_NODE
};
enum passed_rank {
    LASTR, HALFR, ALLR
};
enum search_states {
    NULL_MOVE = 1,NORMAL_MOVE = 2,MOVE_MASK = 3,PROBCUT_SEARCH = 4,IID_SEARCH = 8,SINGULAR_SEARCH = 16
};
enum move_gen_status {
    GEN_START, GEN_RESET, GEN_AVAIL, GEN_HASHM, GEN_CAPS, GEN_QNONCAPS, 
    GEN_KILLERS = 6, GEN_NONCAPS, GEN_LOSCAPS, GEN_END
};
enum BACKUP_TYPE {
    MINMAX, AVERAGE, MIX, MINMAX_MEM, AVERAGE_MEM, MIX_MEM, CLASSIC, MIX_VISIT, RMS
};
enum ROLLOUT_TYPE {
    MCTS, ALPHABETA
};
enum protocol {
    CONSOLE, XBOARD, UCI
};
enum square_names {
    A1 = 0,B1,C1,D1,E1,F1,G1,H1,
    A2 = 16,B2,C2,D2,E2,F2,G2,H2,
    A3 = 32,B3,C3,D3,E3,F3,G3,H3,
    A4 = 48,B4,C4,D4,E4,F4,G4,H4,
    A5 = 64,B5,C5,D5,E5,F5,G5,H5,
    A6 = 80,B6,C6,D6,E6,F6,G6,H6,
    A7 = 96,B7,C7,D7,E7,F7,G7,H7,
    A8 = 112,B8,C8,D8,E8,F8,G8,H8
};

#define RR    0x01
#define LL   -0x01
#define RU    0x11
#define LD   -0x11
#define UU    0x10
#define DD   -0x10
#define LU    0x0f
#define RD   -0x0f

#define RRU   0x12
#define LLD  -0x12
#define LLU   0x0e
#define RRD  -0x0e
#define RUU   0x21
#define LDD  -0x21
#define LUU   0x1f
#define RDD  -0x1f

#define UUU   0x20
#define DDD  -0x20
#define RRR   0x02
#define LLL  -0x02

#define KM       1
#define QM       2
#define RM       4
#define BM       8
#define NM      16
#define WPM     32
#define BPM     64
#define QRBM    14
#define KNM     17

#define AGE_MASK 0xf

#define MAX_STR            256
#define MAX_FILE_STR      3072
#define MAX_FEN_STR        256
#define MAX_MOVES          256
#define MAX_MOVES_NN        80
#define MAX_CAPS            64
#define MAX_PLY             64
#define MAX_HSTACK        1024
#define MATE_SCORE       20000
#define SKIP_SCORE       15000
#define WIN_SCORE         3000
#define WIN_PLY             20
#define MAX_NUMBER    16777216
#define MAX_HIST         16384
#define MAX_UINT64   uint64_t(0xffffffffffffffff)
#define MAX_EGBB             6

#define WSC_FLAG            1
#define WLC_FLAG            2
#define BSC_FLAG            4
#define BLC_FLAG            8
#define WSLC_FLAG           3
#define BSLC_FLAG          12
#define WBC_FLAG           15

/*
Scorpio moves are 32bit long
*/
#define FROM_FLAG        0x000000ff
#define TO_FLAG          0x0000ff00
#define PIECE_FLAG       0x000f0000
#define CAPTURE_FLAG     0x00f00000
#define PROMOTION_FLAG   0x0f000000
#define EP_FLAG          0x10000000
#define CASTLE_FLAG      0x20000000
#define FROM_TO_PROM     0x0f00ffff
#define FROM_TO_PROM_CAS 0x2f00ffff
#define CAP_PROM         0x0ff00000
#define EP_CASTLE_PROM   0x3f000000
#define m_from(x)        (int)((x) & FROM_FLAG)
#define m_to(x)          (int)(((x) & TO_FLAG) >> 8)
#define m_piece(x)       (int)(((x) & PIECE_FLAG) >> 16)
#define m_capture(x)     (int)(((x) & CAPTURE_FLAG) >> 20)
#define m_promote(x)     (int)(((x) & PROMOTION_FLAG) >> 24)
#define is_cap(x)        ((x) & CAPTURE_FLAG)
#define is_prom(x)       ((x) & PROMOTION_FLAG)
#define is_cap_prom(x)   ((x) & CAP_PROM)
#define is_ep(x)         ((x) & EP_FLAG)
#define is_castle(x)     ((x) & CASTLE_FLAG)
#define is_special(x)    ((x) & EP_CASTLE_PROM)
/*
Squares are of 0x88 type
*/
#define file(x)          ((x) &  7)
#define rank(x)          ((x) >> 4)
#define file64(x)        ((x) &  7)
#define rank64(x)        ((x) >> 3)
#define SQ(x,y)          (((x) << 4) | (y))
#define SQ64(x,y)        (((x) << 3) | (y))
#define SQ32(x,y)        (((x) << 2) | (y))
#define SQ8864(x)        (((x) + ((x) & 7)) >> 1)
#define SQ6488(x)        ((x) + ((x) & 070))
#define SQ6448(x)        ((x) - 8)
#define SQ4864(x)        ((x) + 8)   
#define MIRRORF(sq)      ((sq) ^ 0x07)
#define MIRRORR(sq)      ((sq) ^ 0x70)
#define MIRRORD(sq)      SQ(file(sq),rank(sq))
#define MIRRORF64(sq)    ((sq) ^ 007)
#define MIRRORR64(sq)    ((sq) ^ 070)
#define MIRRORD64(sq)    SQ64(file64(sq),rank64(sq))
#define MV8866(x)        (SQ8864(m_from(x)) | (SQ8864(m_to(x)) << 6))
#define is_light(x)      ((file(x)+rank(x)) & 1)
#define is_light64(x)    ((file64(x)+rank64(x)) & 1)

/*
piece and color
*/
#define PCOLOR(x)        ((x) > 6)
#define PIECE(x)         (pic_tab[x]) 
#define COMBINE(c,x)     ((x) + (c) * 6) 
#define invert(x)        (!(x))
/*
distances
*/
#define MAX(a, b)        (((a) > (b)) ? (a) : (b))
#define MIN(a, b)        (((a) < (b)) ? (a) : (b))
#define ABS(a)           abs(a)
#define f_distance(x,y)  ABS(file(x)-file(y))
#define r_distance(x,y)  ABS(rank(x)-rank(y))
#define distance(x,y)            MAX(f_distance(x,y),r_distance(x,y))
#define DISTANCE(f1,r1,f2,r2)    MAX(ABS((f1) - (f2)),ABS((r1) - (r2)))
/*
hash keys
*/
#define PC_HKEY(p,sq)    (piece_hkey[p][SQ8864(sq)])
#define EP_HKEY(ep)      (ep_hkey[file(ep)])
#define CAST_HKEY(c)     (cast_hkey[c])
/*
PGN/EPD parallel processor class
*/
class ParallelFile {
private:
    char* memmap_file;
public:
    FILE* f;
    LOCK lock;
    unsigned count;
    bool open(const char*, bool = false);
    void close();
    void rewind();
    bool is_open() { return (f != 0); }
    ParallelFile() { f = 0; }
    virtual bool next(char*, bool = false) = 0;
};
class PGN : public ParallelFile {
public:
    bool next(char*, bool = false) override;
};
class EPD : public ParallelFile {
public:
    bool next(char*, bool = false) override;
};
/*
chess clock
*/
typedef struct CHESS_CLOCK {
    int mps;
    int p_inc;
    int o_inc;
    int p_time;
    int o_time;
    int max_st;
    int max_sd;
    int max_visits;
    int search_time;
    int maximum_time;
    int infinite_mode;
    int pondering;
    CHESS_CLOCK();
    void set_stime(int,bool);
    bool is_timed();
}*PCHESS_CLOCK;
/*
piece list
*/
typedef struct LIST{
    int   sq;
    LIST* prev;
    LIST* next;
}*PLIST;
/*
score
*/
typedef struct SCORE {
    int16_t mid;
    int16_t end;
    SCORE() {}
    SCORE(int m,int e) {mid = m;end = e;}
    void zero() {mid = 0;end = 0;}
    void add(SCORE s) {mid += s.mid;end += s.end;}
    void sub(SCORE s) {mid -= s.mid;end -= s.end;}
    void add(int m,int e) {mid += m;end += e;}
    void sub(int m,int e) {mid -= m;end -= e;}
    void add(int x) {mid += x;end += x;}
    void sub(int x) {mid -= x;end -= x;}
    void addm(int m) {mid += m;}
    void subm(int m) {mid -= m;}
    void adde(int e) {end += e;}
    void sube(int e) {end -= e;}
} *PSCORE;
/*
hash tables
*/
typedef struct tagHASH {
    uint32_t check_sum;
    int32_t eval;
    MOVE move;
    union {
        uint32_t data;
        struct {
            int16_t score;
            uint8_t depth;
            uint8_t flags;
        };
    };
} HASH,*PHASH;

typedef struct tagPAWNREC {
    uint8_t w_passed;
    uint8_t b_passed;
    uint8_t w_pawn_f;
    uint8_t b_pawn_f;
    uint8_t w_ksq;
    uint8_t b_ksq;
    uint8_t w_attack;
    uint8_t b_attack;
    int8_t w_s_attack;
    int8_t b_s_attack;
} PAWNREC,*PPAWNREC;

typedef struct tagPAWNHASH {
    uint16_t check_sum;
    SCORE   score;
    PAWNREC pawnrec;
} PAWNHASH, *PPAWNHASH;

typedef struct tagEVALHASH {
    uint16_t  check_sum;
    int16_t   score;
} EVALHASH,*PEVALHASH;

struct SEARCHER;
struct PROCESSOR;
/*
* Edges of the tree
*/
class Edges {
private:
    uint16_t* data;

    /*16-bit encoding of policy ala lc0*/
    uint16_t get_halfp(int32_t p) const {
      p += ((1 << 11) - (3 << 28));
      return (p < 0) ? 0 : (uint16_t)(p >> 12);
    }

    float get_float(uint16_t p) const {
      uint32_t r = ((uint32_t)(p) << 12) | (3 << 28);
      float ret;
      memcpy(&ret, &r, sizeof(r));
      return ret;
    }
public:
    float score;
    std::atomic_ushort count;
    std::atomic_ushort n_children;

    uint16_t get_move(int idx) const {
        return data[idx];
    }
    void set_move(int idx, uint16_t move) {
        data[idx] = move;
    }
    float get_score(int idx) const {
        return get_float(data[count + idx]);
    };
    void set_score(int idx, int count_, int32_t score) {
        data[count_ + idx] = get_halfp(score);
    };

    enum { CREATE = (1 << 14) };

    bool try_create() { return !(l_or(n_children,CREATE) & CREATE); }
    void clear_create() { l_and(n_children,~CREATE); };
    void inc_children() { l_add(n_children,1); };
    unsigned short get_children() { return (n_children & ~CREATE); }

    void clear() {
        count = 0;
        data = 0;
        n_children = 0;
    }

    static void allocate(Edges&, int, int);
    static void reclaim(Edges&, int);
    static std::vector<uint16_t*> mem_[MAX_CPUS][MAX_MOVES_NN >> 3];
};
/*
* Nodes of the tree
*/
typedef void (CDECL *PTHREAD_PROC) (void*);
void CDECL gc_thread_proc(void*);
void CDECL rank_reset_thread_proc(void*);
void CDECL convert_score_thread_proc(void*);

struct Node {

    /*64 bytes data*/
    Node* child;
    Node* next;
    Edges edges;
    MOVE move;
    float policy;
    union {
        float prior;
        float v_pol_sum;
    };
    std::atomic_uint visits;
    std::atomic<float> score;
    std::atomic_ushort busy;
    std::atomic_uchar flag;
    unsigned char rank;
    union {
        std::atomic_int alpha;
        std::atomic<float> reg_policy;
    };
    union {
        std::atomic_int beta;
        std::atomic<float> Q;
    };

    /*accessors*/
    enum {
        SCOUTF = 1, PVMOVE = 2, CREATE = 4, DEAD = 8
    };
    
    void inc_busy() { l_add(busy,1); }
    void dec_busy() { l_add(busy,-1); }
    int  get_busy() { return busy; }

    void set_pvmove() { l_or(flag,PVMOVE); }
    void clear_pvmove() { l_and(flag,~PVMOVE); }
    bool is_pvmove() { return (flag & PVMOVE); }

    void set_dead() { l_or(flag,DEAD); }
    void clear_dead() { l_and(flag,~DEAD); }
    bool is_dead() { return (flag & DEAD); }

    bool try_failed_scout() { return !(l_or(flag,SCOUTF) & SCOUTF); }
    void clear_failed_scout() { l_and(flag,~SCOUTF); }
    bool is_failed_scout() { return (flag & SCOUTF); }

    bool try_create() { return !(l_or(flag,CREATE) & CREATE); }
    void clear_create() { l_and(flag,~CREATE); }
    bool is_create() { return (flag & CREATE); }

    void set_bounds(int a,int b) {
        l_set(alpha,a);
        l_set(beta,b);
    }

    void update_visits(unsigned int v) { l_add(visits,v); }
    void update_score(float s) { score = s; } /* Assume atomic until fixed! */

    Node* add_child(int,int,MOVE,float,float);
    Node* add_null_child(int,float);
    float compute_Q(float,unsigned int, bool);
    float compute_fpu(int);

    void clear() {
        score = 0;
        v_pol_sum = 0;
        visits = 0;
        child = 0;
        next = 0;
        rank = 0;
        flag = 0;
        busy = 0;
        move = MOVE();
        alpha = -MATE_SCORE;
        beta = MATE_SCORE;
        edges.clear();
    }
    static std::atomic_uint total_nodes;
    static unsigned int max_tree_nodes;
    static unsigned int max_tree_depth;
    static unsigned int sum_tree_depth;
    static std::vector<Node*> mem_[MAX_CPUS];
    static Node* allocate(int);
    static void  split(Node*, std::vector<Node*>*, const int, int&);
    static void  reclaim(Node*,int);
    static void  rank_children(Node*);
    static void  convert_score(Node*);
    static void  reset_bounds(Node*);
    static void  parallel_job(Node*, PTHREAD_PROC, bool = false);
    static Node* print_tree(Node*,bool,int = 0,int = 0);
    static Node* Max_UCB_select(Node*,bool,SEARCHER*);
    static Node* Max_AB_select(Node*,int,int,bool,bool,SEARCHER*);
    static Node* ExactPi_select(Node*,bool,SEARCHER*);
    static Node* Best_select(Node*,bool);
    static Node* Random_select(Node*,int);
    static float Min_score(Node*);
    static float Avg_score(Node*);
    static float Avg_score_mem(Node*,float,int);
    static float Rms_score_mem(Node*,float,int);
    static float Max_visits_score(Node*);
    static void Backup(Node*,float&,int);
    static void BackupLeaf(Node*,float&);
    static void print_xml(Node*,int);
    static float compute_policy_sum_reverseKL(Node*,float,float,float);
    static float compute_regularized_policy_reverseKL(Node*,float,float,bool);
};
/*
NNUE - data structures for incemental update
*/
#ifdef NNUE_INC

typedef struct DirtyPiece {
  int dirtyNum;
  int pc[3];
  int from[3];
  int to[3];
} DirtyPiece;

typedef struct Accumulator {
  CACHE_ALIGN int16_t accumulation[2][256];
  int computedAccumulation;
} Accumulator;

typedef struct NNUEdata {
  Accumulator accumulator;
  DirtyPiece dirtyPiece;
} NNUEdata;

#endif
/*
stacks
*/
typedef struct HIST_STACK{
    MOVE move;
    int castle;
    int epsquare;
    int fifty;
    int checks;
    int rev_check;
    PLIST pCapt;
    PLIST pProm;
    SCORE pcsq_score[2];
    uint64_t all_bb;
    HASHKEY hash_key;
    HASHKEY pawn_hash_key;
    uint64_t pieces_bb[2];
    uint64_t pawns_bb[2];
    static char start_fen[MAX_FEN_STR];
} *PHIST_STACK;

typedef struct STACK{
    int pv_length;
    MOVE current_move;
    int count;
    int current_index;
    int bad_index;
    int gen_status;
    int sortm;
    int best_score;
    MOVE best_move;
    MOVE hash_move;
    int hash_flags;
    int hash_depth;
    int hash_score;
    int hash_eval;
    int extension;
    int reduction;
    int mate_threat;
    int singular;
    int legal_moves;
    int alpha;
    int beta;
    int depth;
    int o_alpha;
    int o_beta;
    int o_depth;
    int node_type;
    int next_node_type;
    int flag;
    int search_state;
    int noncap_start;
    int static_eval;
    bool improving;
    bool all_done;
    bool second_pass;
    MOVE killer[2];
    MOVE refutation;
    int qcheck_depth;
    uint64_t start_nodes;
    MOVE move_st[MAX_MOVES];
    int score_st[MAX_MOVES];
    MOVE bad_st[MAX_CAPS];
    MOVE pv[MAX_PLY];
    void sort_1(const int,const int);
    void sort_all();
} *PSTACK;

/*
 * MESSAGES
 */
#ifdef CLUSTER
/*
 * Messages are made to be of known size on all platforms so that
 * construction of MPI_datatypes is avoided. Also simple transfer of messages
 * in bytes i.e. sizeof(MESSAGE) * MPI_BYTE is more efficient at least in theory
 */
struct SPLIT_MESSAGE {
    int64_t  master;
    int32_t  depth;
    int32_t  alpha;
    int32_t  beta;
    int32_t  node_type;
    int32_t  search_state;
    int32_t  extension;
    int32_t  reduction;
    int32_t  pv_length;
    MOVE   pv[MAX_PLY];
};
struct MERGE_MESSAGE {
    int64_t  master;
    uint64_t nodes;
    uint64_t qnodes;
    uint64_t ecalls;
    uint64_t nnecalls;
    uint64_t time_check;
    uint32_t splits;
    uint32_t bad_splits;
    uint32_t egbb_probes;
    MOVE   best_move;
    int32_t  best_score;
    int32_t  pv_length;
    MOVE   pv[MAX_PLY];
};
struct INIT_MESSAGE {
    int8_t fen[MAX_FEN_STR];
    int32_t pv_length;
    MOVE  pv[127];
};
struct PV_MESSAGE {
    int32_t pv_length;
    MOVE  pv[MAX_PLY];
};
struct TT_MESSAGE {
    uint64_t hash_key;
    int16_t  score;
    uint8_t  depth;
    uint8_t  flags;
    uint8_t  ply;
    uint8_t  col;
    uint8_t  mate_threat;
    uint8_t  singular;
    MOVE   move;
    int16_t  alpha;
    int16_t  beta;
};

#define   SPLIT_MESSAGE_SIZE(x)   (40 + ((x).pv_length << 2))
#define   RESPLIT_MESSAGE_SIZE(x) (SPLIT_MESSAGE_SIZE(x) + 4)
#define   MERGE_MESSAGE_SIZE(x)   (72 + ((x).pv_length << 2))
#define   INIT_MESSAGE_SIZE(x)    (MAX_FEN_STR + 4 + ((x).pv_length << 2))
#define   PV_MESSAGE_SIZE(x)      (4 + ((x).pv_length << 2))

#endif
/*
SEARCHER
*/
typedef struct SEARCHER{
    /*
    data that needs to be copied by COPY fn below
    */
    int* const board;
    PSTACK pstack;
    int player;
    int opponent;
    int castle;
    int epsquare;
    int fifty;
    int hply;
    int ply;
    int stop_ply;
    int pawn_c[2];
    int piece_c[2];
    int man_c[15];
    int all_man_c;
    uint64_t all_bb;
    uint64_t pieces_bb[2];
    uint64_t pawns_bb[2];
    HASHKEY hash_key;
    HASHKEY pawn_hash_key;
    SCORE pcsq_score[2];
    int temp_board[192];
    LIST temp_list[128];
    PLIST list[128];
    PLIST plist[15];
    int frc_squares[6];
    HIST_STACK hstack[MAX_HSTACK];
#ifdef NNUE_INC
    NNUEdata* nnue;
#endif
    STACK stack[MAX_PLY];
    float prev_kld;
    /*eval data*/
    PAWNREC pawnrec;
    /*functions*/
    SEARCHER();
    void  COPY(SEARCHER*);
    void  operator = (SEARCHER& right) { COPY(&right); }
    void  set_board(const char* fen_str);
    void  get_fen(char* fen_str) const;
    void  new_board();
    void  mirror();
    void  init_data();
    void  pcAdd(int,int,PLIST = 0);
    void  pcRemove(int,int,PLIST&);
    void  pcSwap(int,int);
    void  do_move(const MOVE&);
    void  do_null();
    void  undo_move();
    void  undo_null();
    HASHKEY get_key_after(MOVE move);
    void  PUSH_MOVE(MOVE);
    void  PUSH_NULL();
    void  POP_MOVE();
    void  POP_NULL();
    void  UPDATE_PV(MOVE);
    int   attacks(int,int) const;
    int   checks(MOVE,int&) const;
    int   in_check(MOVE) const;
    int   is_legal(MOVE&);
    int   is_legal_fast(MOVE);
    int   is_pawn_push(MOVE move) const;
    int   pinned_on_king(int,int) const;
    bool  can_castle(bool);
    void  print_board() const;
    void  print_history();
    void  print_stack();
    void  print_game(int,FILE* = 0,const char* event = 0, 
                    const char* whitep = 0, 
                    const char* blackp = 0, int Round = 0);
    void  print_allmoves();
    int   see(MOVE);
    void  gen_caps(bool = false);
    void  gen_noncaps();
    void  gen_checks();
    void  gen_evasions();
    void  gen_all();
    MOVE  get_move();
    MOVE  get_qmove();
    void  gen_all_legal();
    int   draw(int = 0) const;
    MOVE  find_best();
    MOVE  iterative_deepening(bool&);
    int   be_selective(int,bool);
    int   on_node_entry();
    int   on_qnode_entry();
    void  search();
    void  qsearch();
    void  qsearch_nn();
    bool  hash_cutoff();
    uint64_t   perft(int);
    MOVE  get_book_move();
    void  show_book_moves();
    void  print_pv(int);
    int   print_result(bool);
    void  check_quit();
    void  check_mcts_quit(bool);
    void  compute_time_factor(int);
    double compute_kld();
    int   eval(bool = false);
    int   eval_hce();
    void  eval_pawn_cover(int,int,uint8_t*,uint8_t*);
    SCORE eval_pawns(int,int,uint8_t*,uint8_t*);
    int   eval_passed_pawns(uint8_t*,uint8_t*,uint8_t&,const uint64_t&,const uint64_t&);
    void  eval_imbalance(SCORE&,SCORE&);
    void  eval_win_chance(int&,int&);
    static void  pre_calculate();
#ifdef TUNE
    void  update_pcsq(int,int,int);
    void  update_pcsq_val(int,int,int);
    void  tune(int,char*);
#endif
    void  record_hash(int,const HASHKEY&,int,int,int,int,int,MOVE,int,int);
    void  RECORD_HASH(int,const HASHKEY&,int,int,int,int,int,MOVE,int,int);
    int   probe_hash(int,const HASHKEY&,int,int,int&,int&,MOVE&,int,int,int&,int&,int&,bool);
    int   PROBE_HASH(int,const HASHKEY&,int,int,int&,int&,MOVE&,int,int,int&,int&,int&,bool);
    void  record_pawn_hash(const HASHKEY&,const SCORE&,const PAWNREC&);
    int   probe_pawn_hash(const HASHKEY&,SCORE&,PAWNREC&);
    void  record_eval_hash(const HASHKEY&,int);
    int   probe_eval_hash(const HASHKEY&,int&);
    void  prefetch_tt(const HASHKEY&);
    bool  san_mov(MOVE& move,char* s);
    void  str_mov(MOVE& move,char* s);
    void  mov_str(const MOVE& move,char* s);
    bool  build_book(char*,char*,int,int,int);
    void  pgn_to_epd(char*,FILE*,int=0);
    void  epd_to_nn(char*,FILE*,int=0);
    static void  allocate_history();
    void  update_history(MOVE,bool=false);
    void  clear_history();
    int   get_history_score(const MOVE&);
    int   search_ab();
    void  search_ab_prior();
    void  evaluate_moves(int);
    float  generate_and_score_moves(int,int);
    int   Random_select_ab();
    void  boost_policy();
    void  unboost_policy();
    /*mcts stuff*/
    void  extract_pv(Node*,bool=false);
    void  create_children(Node*);
    void  manage_tree(bool=false);
    void  play_simulation(Node*,float&,int&);
    void  search_mc(bool=false, unsigned int = 0);
    void  print_status();
    void  idle_loop_main();
    MOVE decode_move(uint16_t);
    uint16_t encode_move(MOVE);
    /*counts*/
    uint64_t nodes;
    uint64_t qnodes;
    uint64_t ecalls;
    uint64_t nnecalls;
    uint64_t time_check;
    uint64_t message_check;
    uint32_t search_calls;
    uint32_t qsearch_calls;
    uint32_t splits;
    uint32_t bad_splits;
    uint32_t egbb_probes;
    uint32_t seldepth;
    std::atomic_int stop_searcher;
    bool finish_search;
    bool skip_nn;
    /*
    Parallel search
    */
    bool used;
    int  processor_id;
    SEARCHER* master;
    void handle_fail_high();
    void clear_block();
    LOCK lock;
    std::atomic_int n_workers;
    std::atomic<SEARCHER*> workers[MAX_CPUS];

    int get_smp_move();
    int check_split();
    void attach_processor(int);
    void update_master(int);
    void stop_workers();
#ifdef CLUSTER
    std::atomic_int n_host_workers;
    std::list<int> host_workers;
    int get_cluster_move(SPLIT_MESSAGE*,bool=false);
    void get_init_pos(INIT_MESSAGE*);
#endif
    Node* root_node;
    HASHKEY root_key;
    void copy_root(SEARCHER* src) {
        root_node = src->root_node;
        root_key  = src->root_key;
    }
    /*
    things that are shared among multiple searchers.
    */
    static std::atomic_uint playouts;
    static int search_depth;
    static int start_time;
    static int start_time_o;
    static int scorpio;
    static int pv_print_style;
    static int root_score;
    static int old_root_score;
    static int root_failed_low;
    static float time_factor;
    static int last_book_move;
    static int first_search;
    static int analysis_mode;
    static int show_full_pv;
    static int abort_search;
    static uint32_t poll_nodes;
    static MOVE expected_move;
    static int resign_value;
    static int resign_count;
    static unsigned int average_pps;
    static CHESS_CLOCK chess_clock;
    static uint64_t root_nodes[MAX_MOVES];
    static int root_scores[MAX_MOVES];
    static int16_t history[14][64];
    static int16_t history_ply[64][6][64];
    static MOVE refutation[14][64];
    static int16_t* ref_fup_history;
    /*
    Bitbases and neural network
    */
    void fill_list(int&,int*,int*);
    int probe_bitbases(int&);
    bool bitbase_cutoff();
    void ensemble_net(int,int,int,float&);
    float probe_neural_(bool,float*,int,int,int);
    float probe_neural(bool=false);
    int probe_nnue();
    bool handle_collisions(Node*);
    bool handle_terminals(Node*);
    void prefetch_nodes(int);
    void self_play_thread();
    void self_play_thread_all(FILE*,FILE*,int);
    void worker_thread();
    void worker_thread_all(ParallelFile*,FILE*,int,bool=false);
    void launch_worker_threads();
    void fill_input_planes(float**,int);
    void write_input_planes(FILE*);
    int compress_input_planes(float**, char*);
    void select_net();
    void get_train_data(float&, int&, int*, float*, float*, int&);
    int compute_move_index(MOVE&, int = -1);
    static bool has_ab;
    static int egbb_is_loaded;
    static int egbb_load_type;
    static int egbb_depth_limit;
    static int egbb_ply_limit_percent;
    static int egbb_ply_limit;
    static int egbb_cache_size;
    static char egbb_path[MAX_STR];
    static char egbb_files_path[MAX_STR];
    static char nn_path[MAX_STR];
    static char nn_path_e[MAX_STR];
    static char nn_path_m[MAX_STR];
    static char nnue_path[MAX_STR];
    static int nn_cache_size;
    static int nn_cache_size_m;
    static int nn_cache_size_e;
    static int use_nnue;
    static int use_nn;
    static int save_use_nn;
    static int n_devices;
    static int device_type;
    static int delay;
    static int float_type;
    static int nn_id;
    static int nn_type;
    static int nn_type_e;
    static int nn_type_m;
    static int wdl_head;
    static int wdl_head_m;
    static int wdl_head_e;
    static int nn_man_e;
    static int nn_man_m;
    static int nnue_scale;
    static int nnue_type;
    static int batch_size_factor;
    static int scheduling;
    static unsigned int root_node_reuse_visits;
    /*
    End
    */
} *PSEARCHER;

void search(PROCESSOR* const, bool = false);
/*
inline piece list functions
*/
FORCEINLINE void SEARCHER::pcAdd(int pic,int sq,PLIST pCapt) {
    PLIST& pHead = plist[pic];
    PLIST pPc = list[sq];

    if(!pHead) {
        pHead = pPc;
        pHead->next = 0;
        pHead->prev = 0;
    } else {
        if(pCapt) {
            pPc->prev = pCapt;
            if(pCapt->next) pCapt->next->prev = pPc;
            pPc->next = pCapt->next;
            pCapt->next = pPc;
        } else {
            pPc->next = pHead;
            pHead->prev = pPc;
            pPc->prev = 0;
            pHead = pPc;
        }
    }
}
FORCEINLINE void SEARCHER::pcRemove(int pic,int sq,PLIST& pCapt) {
    PLIST& pHead = plist[pic];
    PLIST pPc = list[sq];

    pCapt = pPc->prev; 
    if(pPc->next) pPc->next->prev = pPc->prev;
    if(pPc->prev) pPc->prev->next = pPc->next;
    if(pHead == pPc) pHead = pHead->next;
}
FORCEINLINE void SEARCHER::pcSwap(int from,int to) {
    PLIST pPc;
    PLIST& pTo = list[to];
    PLIST& pFrom = list[from];
    pPc = pTo;
    pTo = pFrom;
    pFrom = pPc;
    pTo->sq = to;
    pFrom->sq = from;
}
FORCEINLINE void SEARCHER::PUSH_MOVE(MOVE move) {
    do_move(move);
    ply++;
    pstack++;
}
FORCEINLINE void SEARCHER::PUSH_NULL() {
    do_null();
    ply++;
    pstack++;
}
FORCEINLINE void SEARCHER::POP_MOVE() {
    ply--;
    pstack--;
    undo_move();
}
FORCEINLINE void SEARCHER::POP_NULL() {
    ply--;
    pstack--;
    undo_null();
}
/*
history
*/
#define HISTORY(move) (history[m_piece(move)][SQ8864(m_to(move))])
#define HISTORY_PLY(move,ply) (history_ply[ply][PIECE(m_piece(move)) - 1][SQ8864(m_to(move))])
#define REFUTATION(move) (refutation[m_piece(move)][SQ8864(m_to(move))])
#define REF_FUP_HISTORY(movec,move) (\
            ref_fup_history[m_piece(movec)*64*14*64 + SQ8864(m_to(movec))*14*64 + \
                            m_piece(move)*64 + SQ8864(m_to(move))])
/*
sort move list
*/
#define SWAP(ii,jj) {                   \
    if(ii != jj) {                      \
        MOVE tempm = move_st[ii];       \
        move_st[ii] = move_st[jj];      \
        move_st[jj] = tempm;            \
        int temps = score_st[ii];       \
        score_st[ii] = score_st[jj];    \
        score_st[jj] = temps;           \
    }                                   \
}

FORCEINLINE void STACK::sort_1(const int start,const int end) {
    int bi = start, bs = score_st[start];
    for(int i = start + 1; i < end; i++) {
        if(score_st[i] > bs) {
            bi = i;
            bs = score_st[i];
        }
    }
    SWAP(start,bi);
}
inline void STACK::sort_all() {
    for(int i = 0; i < count; i++)
        sort_1(i, count);
}
/*
PROCESSOR
*/
enum thread_states {
    PARK,WAIT,GO,KILL,GOSP
};
typedef struct PROCESSOR {
    /*searchers*/
    SEARCHER searchers[MAX_SEARCHERS_PER_CPU];
    PSEARCHER searcher;
    std::atomic_int state, parked;

    /*processor count*/
    static int n_processors;
    static int n_cores;
    static std::atomic_int n_idle_processors;
    static int n_hosts;

    /*cluster*/
#ifdef CLUSTER
    enum processor_states {
        QUIT = 0,INIT,HELP,CANCEL,SPLIT,MERGE,PING,PONG,
        PV,STRING,STRING_CMD,GOROOT,RECORD_TT,PROBE_TT,PROBE_TT_RESULT
    };
    static const char *const message_str[15];
    static int host_id;
    static char host_name[256];
    static int help_messages;
    static int prev_dest;
    static std::list<int> available_host_workers;
    static std::atomic_int message_available;
    static std::vector<PV_MESSAGE> node_pvs;
    static std::vector<Node*> pv_tree_nodes;
    static void cancel_idle_hosts();
    static void quit_hosts();
    static void wait_hosts();
    static int CLUSTER_SPLIT_DEPTH;
    static int vote_weight;
    static void set_mt_state(int);
#endif

    /*functions*/
    static void exit_scorpio(int);
    static int SMP_SPLIT_DEPTH;
    static void create(int id);
    static void kill(int id);
    static void park(int id);
    static void wait(int id);
    static void set_num_searchers();
    static void set_main();
#ifdef CLUSTER
    static void init_mpi(int argc, char* argv[]);
    static void init_mpi_thread();
    static void ISend(int dest,int message);
    static void ISend(int dest,int message,void* data,int size,MPI_Request* = 0);
    static void Recv(int dest,int message);
    static void Recv(int dest,int message,void* data,int size);
    static bool IProbe(int& dest,int& message_id);
    static void Wait(MPI_Request*);
    static void Barrier();
    static void Sum(float* sendbuf,float* recvbuf, int);
    static void Max(int* sendbuf,int* recvbuf, int);
    static void Gather(char* sendbuf,char* recvbuf,int,int);
    static void handle_message(int dest,int message_id);
    static void offer_help();
    static void send_pv(int dest, const PV_MESSAGE&);
    static void send_string(const char*);
    static void send_cmd(const char*);
    static void send_init(int dest, const INIT_MESSAGE&);
#endif
    bool has_block();
    void idle_loop();
    static void message_idle_loop();

    /*hash tables*/
    PHASH hash_tab[2];
    PEVALHASH eval_hash_tab[2];
    PPAWNHASH pawn_hash_tab;
#ifdef CLUSTER
    TT_MESSAGE ttmsg;
    std::atomic_bool ttmsg_recieved;
#endif
    static uint32_t hash_tab_mask;
    static uint32_t pawn_hash_tab_mask;
    static uint32_t eval_hash_tab_mask;
    static int age;

    void reset_hash_tab(int id,size_t = 0);
    void reset_pawn_hash_tab(size_t = 0);
    void reset_eval_hash_tab(size_t = 0);
    void delete_tables();
    static void clear_tables(int);
    static void clear_hash_tables();
    static int hashfull();

    /*conditional wait*/
    MUTEX wait_lock;
    COND  wait_cond;
    void signal() {
        std::unique_lock<std::mutex> lk(wait_lock);
        c_signal(wait_cond);
    }
    void wait() {
        std::unique_lock<std::mutex> lk(wait_lock);
        c_wait(wait_cond,lk);
    }

    /*constructor*/
    PROCESSOR() {
        state = KILL;
        parked = 0;
        searcher = 0;
        hash_tab[white] = 0;
        hash_tab[black] = 0;
        eval_hash_tab[white] = 0;
        eval_hash_tab[black] = 0;
        pawn_hash_tab = 0;
    }

} *PPROCESSOR;

extern PPROCESSOR processors[MAX_CPUS];

/*
multi processors
*/
void init_smp(int);
extern LOCK  lock_smp;
extern LOCK  lock_io;
extern int use_abdada_smp;

#ifdef CLUSTER
extern int use_abdada_cluster;
#endif
/*
global data
*/
extern const HASHKEY piece_hkey[14][64];
extern const HASHKEY ep_hkey[8];
extern const HASHKEY cast_hkey[16];
extern const HASHKEY fifty_hkey[100];
extern const int piece_cv[14];
extern const int piece_see_v[14];
extern const int pic_tab[14];
extern const int pawn_dir[2];
extern const int piece_mask[14];
extern int16_t pcsq[14][0x80];
extern bool book_loaded;
extern bool log_on;
extern int scorpio_start_time;
extern int montecarlo;
extern int rollout_type;
extern bool freeze_tree;
extern bool is_selfplay;
extern float frac_abprior;
extern float frac_alphabeta;
extern int mcts_strategy_depth;
extern int qsearch_level;
extern int PROTOCOL;
extern int train_data_type;
extern int variant;

extern int win_weight;
extern int draw_weight;
extern int loss_weight;
extern int ensemble;
extern int ensemble_type;
extern std::atomic_int turn_off_ensemble;

/** search options */
extern const int use_nullmove;
extern const int use_selective;
extern const int use_aspiration;
extern const int use_iid;
extern const int use_pvs;
extern int contempt;

/*
utility functions
*/
int   get_time();
void  init_io();
void  remove_log_file();
void  print(const char* format,...);
void  print_log(const char* format,...);
void  print_std(const char* format,...);
void  print_info(const char* format,...);
void  print_cluster(const char* str);
void  print_move(const MOVE&);
void  print_move_full(const MOVE&);
void  print_sq(const int&);
void  print_pc(const int&);
void  print_bitboard(uint64_t);
void  sq_str(const int& ,char*);
void  mov_san(const MOVE& ,char*);
void  mov_strx(const MOVE& ,char*);
void  str_movx(MOVE& ,char*);
int   tokenize(char* , char** , const char* str2 = " =\n\r\t");
bool  read_line(char*);
int   bios_key(void);
void  load_book();
bool  parse_commands(char**);
void  merge_books(char*,char*,char*,double,double);
int   set_affinity(int);
void  get_core_node(int&,int&);
bool  check_search_params(char**,char*,int&);
void  print_search_params();
bool  check_mcts_params(char**,char*,int&);
void  print_mcts_params();
void  fill_input_planes(int, int, int, int, int*, int*, int*, float*, float*);


#ifdef TUNE
bool check_eval_params(char**,char*,int&);
void print_eval_params();
#endif

/*options*/
void print_spin(const char* name, int def, int min, int max);
void print_check(const char* name, int def);
void print_button(const char* name);
void print_path(const char* name, const char* path);
void print_combo(const char* name, const char** combo, int def, int N);
int is_checked(const char*);
/*
elo
*/
#define Kfactor -0.00575646273

FORCEINLINE double logistic(double score, double kf = Kfactor) {
    return 1 / (1 + exp(kf * score));
}

FORCEINLINE double logit(double p, double kf = Kfactor) {
    if(p < 1e-15) p = 1e-15;
    else if(p > 1 - 1e-15) p = 1 - 1e-15;
    return log((1 - p) / p) / Kfactor;
}
/*
Bitbases
*/
void LoadEgbbLibrary(char* path);
/*
Bitboards.
*/
/*
The following low level subroutines are taken from
http://chessprogramming.wikispaces.com/Bitboards . Thanks to
their respective authors.
*/
#   ifdef ARC_64BIT
#if !defined(HAS_POPCNT)
FORCEINLINE int popcnt(uint64_t b) {
    const uint64_t k1 = UINT64(0x5555555555555555);
    const uint64_t k2 = UINT64(0x3333333333333333);
    const uint64_t k4 = UINT64(0x0f0f0f0f0f0f0f0f);
    const uint64_t kf = UINT64(0x0101010101010101);
    b =  b       - ((b >> 1)  & k1); 
    b = (b & k2) + ((b >> 2)  & k2); 
    b = (b       +  (b >> 4)) & k4 ; 
    return (int) ((b * kf) >> 56);
}
#endif
#if !defined(HAS_BSF)
/*
64bit LSB bitscan from Lieserson etal. 
Private bitscan routine (64bit deBruijin) generated using Gerd Isenberg's code.
NB: Table contains squares in 0x88 format.
*/
const uint64_t magic = 0x021c9a5edd467e2b; /* The 01111981 => This Number is Copyrighted by BIRTH :) */
CACHE_ALIGN const unsigned int table[64] =  {
    0,  1,  2,  7,  3,103, 82, 16,
    4, 22,112, 39, 83, 33, 17, 87,
    5, 80, 37, 23, 70,113,115, 48,
    117, 84, 34, 55, 18, 66, 50, 96,
    119,  6,102, 81, 21, 38, 32, 86,
    71, 36, 69,114,116, 54, 65, 49,
    118,101, 20, 85, 35, 68, 53, 64,
    100, 19, 67, 52, 99, 51, 98, 97
};
FORCEINLINE unsigned int first_one(uint64_t b) {
    return table[((b & -b) * magic) >> 58];
}
#else
FORCEINLINE unsigned int first_one(uint64_t b) {
    unsigned int x = bsf(b);
    return SQ6488(x);
}
#endif

#   else

FORCEINLINE int popcnt(uint64_t b) {
    const uint32_t k1 = (0x55555555);
    const uint32_t k2 = (0x33333333);
    const uint32_t k4 = (0x0f0f0f0f);
    const uint32_t kf = (0x01010101);
    uint32_t hi = (uint32_t) (b >> 32);
    uint32_t lo = (uint32_t) (b);
    hi =  hi       - ((hi >> 1)  & k1); 
    hi = (hi & k2) + ((hi >> 2)  & k2); 
    hi = (hi       +  (hi >> 4)) & k4 ;
    lo =  lo       - ((lo >> 1)  & k1); 
    lo = (lo & k2) + ((lo >> 2)  & k2); 
    lo = (lo       +  (lo >> 4)) & k4 ;
    return (int) (((hi + lo) * kf) >> 24);
}
/*Matt taylor's bitscan , optimized for 32 bit systems*/
CACHE_ALIGN const unsigned int table[64] = {
    119, 54,  3, 64,115, 22, 19, 65,
    116, 48, 98, 17,103, 35, 37, 66,
    117, 53,  2,101, 99, 39, 81, 34,
    112, 52,  1, 83, 86, 51,  0, 67,
    118, 55,114,  4,  5, 97,102,  6,
    23,100, 20, 80,  7, 82, 85, 32,
    49,113, 96, 21, 18, 71, 16, 84,
    36, 87, 70, 38, 33, 69, 68, 50
};
FORCEINLINE unsigned int first_one(uint64_t b) {
    unsigned int folded;
    b ^= (b - 1);
    folded = (int) (b ^ (b >> 32));
    return table[folded * 0x78291ACF >> 26];
}

#   endif
/*
Brian Kernighan's sparse bitboard population count
Used for king attack pattern.
*/
#if !defined(ARC_64BIT) || !defined(HAS_POPCNT)
FORCEINLINE int popcnt_sparse(uint64_t b) {
    int count = 0;
    while (b) {
        count++;
        b &= b - 1;
    }
    return count;
}
#endif

/*
flip bitboard along a1h8.
Used to rotate pawn bitboards.
*/
FORCEINLINE uint64_t Rotate(uint64_t b) {
    uint64_t t;
    const uint64_t k1 = UINT64(0x5500550055005500);
    const uint64_t k2 = UINT64(0x3333000033330000);
    const uint64_t k4 = UINT64(0x0f0f0f0f00000000);
    t  = k4 & (b ^ (b << 28));
    b ^=       t ^ (t >> 28) ;
    t  = k2 & (b ^ (b << 14));
    b ^=       t ^ (t >> 14) ;
    t  = k1 & (b ^ (b <<  7));
    b ^=       t ^ (t >>  7) ;
    return b;
}
/*
Some bitboards
*/
extern const uint64_t rank_mask[8];
extern const uint64_t file_mask[8];
extern const uint64_t __unit_bb[0x80];
extern const uint8_t first_bit[0x100];
extern const uint8_t last_bit[0x100];
extern const uint8_t center_bit[0x100];
extern const uint8_t* const _sqatt_pieces;
extern const int8_t* const _sqatt_step;
extern uint64_t in_between[64][64];
/*
Pradu's magic tables
*/
extern const uint64_t knight_magics[64];
extern const uint64_t king_magics[64];
extern const uint64_t magicmoves_r_magics[64];
extern const uint64_t magicmoves_r_mask[64];
extern const uint64_t magicmoves_b_magics[64];
extern const uint64_t magicmoves_b_mask[64];
extern const unsigned int magicmoves_b_shift[64];
extern const unsigned int magicmoves_r_shift[64];
extern const uint64_t* magicmoves_b_indices[64];
extern const uint64_t* magicmoves_r_indices[64];

#define BB(sq)                  (__unit_bb[sq])
#define NBB(sq)                 (__unit_bb[sq + 8])
#define sqatt_pieces(sq)        _sqatt_pieces[sq]
#define sqatt_step(sq)          _sqatt_step[sq]
#define blocked(from,to)        (in_between[SQ8864(from)][SQ8864(to)] & all_bb)
#define king_attacks(sq)        (king_magics[sq])
#define knight_attacks(sq)      (knight_magics[sq])
#define bishop_attacks(sq,occ)  (*(magicmoves_b_indices[sq]+(((occ&magicmoves_b_mask[sq])*magicmoves_b_magics[sq])>>magicmoves_b_shift[sq])))
#define rook_attacks(sq,occ)    (*(magicmoves_r_indices[sq]+(((occ&magicmoves_r_mask[sq])*magicmoves_r_magics[sq])>>magicmoves_r_shift[sq])))
#define queen_attacks(sq,occ)   (bishop_attacks(sq,occ) | rook_attacks(sq,occ))

void initmagicmoves(void);
/*
End
*/
#endif
