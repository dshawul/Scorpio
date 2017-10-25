//Automatically generated file. 
//Any changes made will be lost after tuning. 
static PARAM ATTACK_WEIGHT = 24;
static PARAM TROPISM_WEIGHT = 29;
static PARAM PAWN_GUARD = 21;
static PARAM HANGING_PENALTY = 24;
static PARAM KNIGHT_OUTPOST_MG = 43;
static PARAM KNIGHT_OUTPOST_EG = 13;
static PARAM BISHOP_OUTPOST_MG = 49;
static PARAM BISHOP_OUTPOST_EG = -2;
static PARAM KNIGHT_MOB_MG = 18;
static PARAM KNIGHT_MOB_EG = 49;
static PARAM BISHOP_MOB_MG = 19;
static PARAM BISHOP_MOB_EG = 24;
static PARAM ROOK_MOB_MG = 36;
static PARAM ROOK_MOB_EG = 21;
static PARAM QUEEN_MOB_MG = -62;
static PARAM QUEEN_MOB_EG = 242;
static PARAM PASSER_MG = 22;
static PARAM PASSER_EG = 19;
static PARAM CANDIDATE_PP_MG = 29;
static PARAM CANDIDATE_PP_EG = 40;
static PARAM PAWN_DOUBLED_MG = -2;
static PARAM PAWN_DOUBLED_EG = 9;
static PARAM PAWN_ISOLATED_ON_OPEN_MG = 66;
static PARAM PAWN_ISOLATED_ON_OPEN_EG = 7;
static PARAM PAWN_ISOLATED_ON_CLOSED_MG = 32;
static PARAM PAWN_ISOLATED_ON_CLOSED_EG = 15;
static PARAM PAWN_WEAK_ON_OPEN_MG = 35;
static PARAM PAWN_WEAK_ON_OPEN_EG = 0;
static PARAM PAWN_WEAK_ON_CLOSED_MG = 12;
static PARAM PAWN_WEAK_ON_CLOSED_EG = 1;
static PARAM ROOK_ON_7TH = 14;
static PARAM ROOK_ON_OPEN = 19;
static PARAM ROOK_SUPPORT_PASSED_MG = -34;
static PARAM ROOK_SUPPORT_PASSED_EG = 63;
static PARAM TRAPPED_BISHOP = 91;
static PARAM TRAPPED_KNIGHT = 108;
static PARAM TRAPPED_ROOK = 120;
static PARAM QUEEN_MG = 1756;
static PARAM QUEEN_EG = 1462;
static PARAM ROOK_MG = 839;
static PARAM ROOK_EG = 817;
static PARAM BISHOP_MG = 700;
static PARAM BISHOP_EG = 467;
static PARAM KNIGHT_MG = 670;
static PARAM KNIGHT_EG = 442;
static PARAM PAWN_MG = 142;
static PARAM PAWN_EG = 137;
static PARAM BISHOP_PAIR_MG = 13;
static PARAM BISHOP_PAIR_EG = 23;
static PARAM MAJOR_v_P = 192;
static PARAM MINOR_v_P = 60;
static PARAM MINORS3_v_MAJOR = -30;
static PARAM MINORS2_v_MAJOR = -12;
static PARAM ROOK_v_MINOR = 38;
static PARAM king_pcsq[64] = {
  -7,  20, -20,   0, -68, -36, -30, -35,
  -4,  -3, -33, -41, -26,  -8,   1,   3,
 -41, -35, -41, -38, -14,   3,  18,   9,
 -67, -61, -58, -65,   2,  19,  20,  13,
 -73, -74, -78, -81,   9,  34,  45,  30,
 -79, -75, -74, -79,   0,  22,  37,  30,
 -80, -78, -77, -80, -16,   1,   8,  14,
 -81, -79, -80, -80, -31, -18, -10,  -1,
};
static PARAM queen_pcsq[64] = {
  -6,  -9,  -4,  25,  -2,  -4,  -8,  -6,
  -8,  -1,  25,  20,  -4,  -4,  -2,  -1,
  -8,  16,  -1,   3,  -6,   3,   4,   5,
  -2,  -7,  -2,  -8,  -5,  -1,   6,  10,
  -9,  -6,  -4, -10, -11,  -1,   6,  13,
   2,   2,  -6,   0, -14,   0,   3,   8,
  -4, -25,   1,  -3, -19,  -3,  -9,  -4,
  -9,  -6,  -2,  -6, -19, -14, -10, -16,
};
static PARAM rook_pcsq[64] = {
  12,   0,  16,  25,  -1,   4,   2, -14,
 -23,   2,  -2,   8,  -5,  -6,  -3,  -4,
 -18,  -2,  -3,   0,  -8,  -4,   1,  -6,
 -13,  -2,  -2,   1,  -1,   2,   4,  -2,
  -4,  -2,   6,   6,   4,  13,   1,   7,
  -5,   6,   5,   6,   3,   8,  12,   7,
  -3,   1,   9,   7,  -2,  11,   8,   4,
  -2,   1,   3,   9,   8,  11,  10,  10,
};
static PARAM bishop_pcsq[64] = {
 -21, -19, -23, -17,  -5, -17, -19, -22,
 -11,  35,   0,  -3,  -5,   2, -14,  -9,
 -16,   5,  17,   1,  -2,  -5,  12,   0,
 -11,  -4, -16,  13, -15,   4,   2,   9,
 -15, -14,   0,   8, -18,  -3,   8,  19,
 -16,  -1,   5,  -3, -11,  -9,  12,   6,
 -17, -10,  -4,  -6, -10,  -8,  -2,  -5,
 -14, -10, -13, -14, -14, -13, -11, -14,
};
static PARAM knight_pcsq[64] = {
 -43, -38, -27, -21, -25, -26, -20, -17,
 -32,  -9,  -6,   7, -18,  -6,  -5,  -6,
 -34,   2,  17,  20,  -1,  -9,   3,   1,
 -16,   0,  13,   9,   0,   0,  15,  13,
 -12,   9,  13,  14, -12,   4,  25,  18,
 -17,   2,   9,  14, -13,  -2,   5,   0,
 -34, -14,  -1,  -9, -17,  -7,  -7, -11,
 -45, -32, -23, -20, -30, -26, -17, -18,
};
static PARAM pawn_pcsq[64] = {
   0,   0,   0,   0,   0,   0,   0,   0,
 -23,  -7, -10, -22,   9,  14,  -1,  -2,
  -3,   9,  11,  17,  -1,  -4,  -6,  -9,
 -17,  -7,  13,  46, -15,  -6,   4,   5,
  -5,  33,  22,  54, -23,  -6,   3,  17,
  -1,  20,  30,  19, -17,  11,  28,  41,
   4,  22,  25,  28,  17,  26,  36,  50,
   0,   0,   0,   0,   0,   0,   0,   0,
};
#ifdef TUNE
static PARAM ELO_HOME = 10;
static PARAM ELO_DRAW = 222;
static PARAM ELO_HOME_SLOPE_PHASE = 42;
static PARAM ELO_DRAW_SLOPE_PHASE = -36;
#endif
