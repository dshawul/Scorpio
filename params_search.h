#ifndef __PARAMS_SEARCH__
#define __PARAMS_SEARCH__

static PARAM aspiration_window = 6;
static PARAM futility_margin[] = {0, 143, 232, 307, 615, 703, 703, 960};
static PARAM failhigh_margin[] = {0, 126, 304, 382, 620, 725, 1280, 0};
static PARAM razor_margin[] = {0, 136, 181, 494, 657, 0, 0, 0};
static PARAM lmp_count[] = {0, 10, 10, 15, 21, 24, 44, 49};
static PARAM lmr_count[] = {4, 6, 7, 8, 13, 20, 25, 31};
static PARAM lmr_all_count = 3;
static PARAM lmr_cut_count = 5;
static PARAM lmr_root_count[] = {4, 8};

static const int use_nullmove = 1;
static const int use_selective = 1;
static const int use_tt = 1;
static const int use_aspiration = 1;
static const int use_iid = 1;
static const int use_ab = 1;
static const int use_pvs = 1;
static const int contempt = 2;

#endif