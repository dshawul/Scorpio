//Automatically generated file. 
//Any changes made will be lost after tuning. 
static PARAM ATTACK_WEIGHT = 21;
static PARAM TROPISM_WEIGHT = 20;
static PARAM PAWN_GUARD = 19;
static PARAM HANGING_PENALTY = 26;
static PARAM KNIGHT_OUTPOST_MG = 49;
static PARAM KNIGHT_OUTPOST_EG = 1;
static PARAM BISHOP_OUTPOST_MG = 51;
static PARAM BISHOP_OUTPOST_EG = -12;
static PARAM KNIGHT_MOB_MG = 29;
static PARAM KNIGHT_MOB_EG = 53;
static PARAM BISHOP_MOB_MG = 19;
static PARAM BISHOP_MOB_EG = 22;
static PARAM ROOK_MOB_MG = 35;
static PARAM ROOK_MOB_EG = 22;
static PARAM QUEEN_MOB_MG = -46;
static PARAM QUEEN_MOB_EG = 245;
static PARAM PASSER_MG = 19;
static PARAM PASSER_EG = 19;
static PARAM CANDIDATE_PP_MG = 25;
static PARAM CANDIDATE_PP_EG = 43;
static PARAM PAWN_DOUBLED_MG = 2;
static PARAM PAWN_DOUBLED_EG = 11;
static PARAM PAWN_ISOLATED_ON_OPEN_MG = 65;
static PARAM PAWN_ISOLATED_ON_OPEN_EG = 8;
static PARAM PAWN_ISOLATED_ON_CLOSED_MG = 31;
static PARAM PAWN_ISOLATED_ON_CLOSED_EG = 12;
static PARAM PAWN_WEAK_ON_OPEN_MG = 35;
static PARAM PAWN_WEAK_ON_OPEN_EG = -2;
static PARAM PAWN_WEAK_ON_CLOSED_MG = 9;
static PARAM PAWN_WEAK_ON_CLOSED_EG = -1;
static PARAM ROOK_ON_7TH = 17;
static PARAM ROOK_ON_OPEN = 13;
static PARAM ROOK_SUPPORT_PASSED_MG = -31;
static PARAM ROOK_SUPPORT_PASSED_EG = 59;
static PARAM TRAPPED_BISHOP = 91;
static PARAM TRAPPED_KNIGHT = 113;
static PARAM TRAPPED_ROOK = 112;
static PARAM QUEEN_MG = 1865;
static PARAM QUEEN_EG = 1498;
static PARAM ROOK_MG = 868;
static PARAM ROOK_EG = 848;
static PARAM BISHOP_MG = 725;
static PARAM BISHOP_EG = 484;
static PARAM KNIGHT_MG = 694;
static PARAM KNIGHT_EG = 454;
static PARAM PAWN_MG = 142;
static PARAM PAWN_EG = 143;
static PARAM BISHOP_PAIR_MG = 15;
static PARAM BISHOP_PAIR_EG = 25;
static PARAM MAJOR_v_P = 192;
static PARAM MINOR_v_P = 60;
static PARAM MINORS3_v_MAJOR = -31;
static PARAM MINORS2_v_MAJOR = -13;
static PARAM ROOK_v_MINOR = 39;
#ifdef TUNE
static PARAM ELO_HOME = 16;
static PARAM ELO_DRAW = 219;
static PARAM ELO_HOME_SLOPE_PHASE = 36;
static PARAM ELO_DRAW_SLOPE_PHASE = -32;
#endif
static PARAM king_pcsq[64] = {
  -7,  29, -10,  11, -72, -41, -34, -36,
  -3,   3, -36, -45, -27,  -9,  -3,   1,
 -39, -38, -44, -41, -12,   0,  18,   3,
 -72, -66, -64, -65,   1,  21,  20,   5,
 -76, -70, -80, -74,   9,  45,  61,  29,
 -85, -70, -72, -79,   1,  39,  64,  41,
 -81, -78, -78, -78, -15,  11,  19,  21,
 -80, -80, -79, -81, -32, -19,  -8,  -3
};
static PARAM queen_pcsq[64] = {
  -5,  -6,  -6,  27,  -7,  -9, -10,  -8,
 -13,   0,  33,  25, -12, -15, -10,   1,
  -8,  26,   3,   9,  -9,  -1,  -1,   8,
   3,  -5,  -2,  -9,  -5,  -6,   8,  12,
 -12,  -8,  -9, -15, -10,   7,  11,  23,
   2,   5, -10,  -5, -11,  -2,   3,  14,
  -6, -48,  -5,  -7, -12,   2, -10,  -2,
 -10,  -6,  -5,  -7, -17, -14,  -5, -18
};
static PARAM rook_pcsq[64] = {
  10,   3,  25,  37,  -8,   0,  -4, -12,
 -31,   3,  -2,  13,  -5,  -1,  -4,  -1,
 -24,  -3,  -7,   1, -10,  -2,   1,  -3,
 -19,  -6,  -6,   0,  -1,   4,   5,   4,
  -8,  -6,   7,   7,   6,  14,   8,   9,
 -10,   6,  11,   7,   4,  10,  13,   8,
  -5,   5,  18,  11,  -2,   6,   7,   4,
 -10,  -1,   2,  14,   9,  14,  12,  14
};
static PARAM bishop_pcsq[64] = {
 -33, -12, -23, -13,  -1, -17, -18, -31,
 -12,  35,   5,  -1,  -7,  -1, -26, -11,
 -11,   0,  23,  -2,   0,  -7,  15,  -3,
 -12,   3, -19,  17, -13,   7,   1,  10,
 -18, -18,   2,  12, -17,   0,   8,  24,
 -21,   4,   4,  -3, -14,  -9,  16,  11,
 -29,  -8,  -6,  -6, -17,  -7,  -4,  -8,
 -15, -15, -20, -21, -14, -15, -14, -18
};
static PARAM knight_pcsq[64] = {
 -48, -35, -31, -24, -19, -18, -23, -16,
 -33, -18,  -4,   4, -18,  -7,  -7,  -9,
 -35,  -1,  12,  22,  -1, -11,   2,   2,
 -24,   0,   9,   2,   3,   1,  20,  18,
  -8,  10,  17,  13, -11,   2,  30,  23,
 -15,  10,  13,  19, -14,  -4,   8,  -6,
 -40, -14,   8,  -7,  -9,  -5,  -7, -14,
 -48, -35, -29, -19, -25, -28, -25, -23
};
static PARAM pawn_pcsq[64] = {
   0,   0,   0,   0,   0,   0,   0,   0,
 -18,  -1,  -2, -12,  13,  15,   0,  -1,
  -3,  12,  19,  24,   0,  -5,  -6,  -7,
 -17,  -5,  19,  53, -14,  -5,   5,   7,
  -5,  39,  29,  59, -22,  -3,   4,  17,
  -5,  26,  50,  26, -21,   4,  30,  43,
  11,  34,  37,  36,  28,  41,  59,  84,
   0,   0,   0,   0,   0,   0,   0,   0
};
static PARAM outpost[32] = {
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   8,   5,  10,  15,
  13,  13,   6,  15,   5,  21,   8,  12,
 -22,  -6,   6,   1,   0,   0,   0,   0
};
static PARAM passed_bonus[8] = {
   0,  10,  11,  21,  41,  86, 134,   0
};
static PARAM qr_on_7thrank[18] = {
 -24,   0,   1, -29, 139,  83, 199, 200,
 200, 200, 200, 200, 200, 200, 200, 200,
 200, 200
};
static PARAM rook_on_hopen[13] = {
 -61, -47, -37, -21,   0,  60,  69,  90,
 100, 110, 130, 140, 150
};
static PARAM king_to_pawns[8] = {
   0,   5,  32,  60, 130, 158, 175, 144
};
static PARAM piece_tropism[8] = {
   0,  61,  60,  21,  -5,   3,   6, -18
};
static PARAM queen_tropism[8] = {
   0,  37, 113,  15, -15, -32,  -7,   1
};
static PARAM file_tropism[8] = {
  85,  94,  67,  53,   9,  -6, -16, -14
};
