#include "scorpio.h"

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

typedef int (CDECL *PPROBE_EGBB) (int player, int w_king, int b_king,
							int piece1, int square1,
							int piece2, int square2,
							int piece3, int square3);

typedef void (CDECL *PLOAD_EGBB) (char* path,int cache_size,int load_options);
static PPROBE_EGBB probe_egbb;

int SEARCHER::egbb_is_loaded;
int SEARCHER::egbb_load_type = LOAD_4MEN;

/*
Load the dll and get the address of the load and probe functions.
*/

#ifdef _MSC_VER
#	ifdef ARC_64BIT
#		define EGBB_NAME "egbbdll64.dll"
#	else
#		define EGBB_NAME "egbbdll.dll"
#	endif
#else
#	ifdef ARC_64BIT
#		define EGBB_NAME "egbbso64.so"
#	else
#		define EGBB_NAME "egbbso.so"
#	endif
#endif

#ifndef _MSC_VER
#    define HMODULE void*
#    define LoadLibrary(x) dlopen(x,RTLD_LAZY)
#    define GetProcAddress dlsym
#endif

int LoadEgbbLibrary(char* main_path,int egbb_cache_size) {

#ifdef EGBB
	HMODULE hmod;
	PLOAD_EGBB load_egbb;
	char path[256];
	char terminator;
	size_t plen = strlen(main_path);
	strcpy(path,main_path);
	if (plen) {
		terminator = main_path[strlen(main_path)-1];
		if (terminator != '/' && terminator != '\\') {
			if (strchr(path, '\\') != NULL)
				strcat(path, "\\");
			else
				strcat(path, "/");
		}
	}
	strcat(path,EGBB_NAME);
	if((hmod = LoadLibrary(path)) != 0) {
		load_egbb = (PLOAD_EGBB) GetProcAddress(hmod,"load_egbb_5men");
     	probe_egbb = (PPROBE_EGBB) GetProcAddress(hmod,"probe_egbb_5men");
        load_egbb(main_path,egbb_cache_size,SEARCHER::egbb_load_type);
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

int SEARCHER::probe_bitbases(int& score) {

#ifdef EGBB

	register PLIST current;
	int piece[3],square[3],count;
	count = 0;
	piece[0] = 0;
	piece[1] = 0;
	piece[2] = 0;
	square[0] = 0;
	square[1] = 0;
	square[2] = 0;
	
#define ADD_PIECE(list,type) {					\
	   current = list;							\
	   while(current) {							\
	      piece[count] = type;					\
		  square[count] = SQ8864(current->sq);	\
		  current = current->next;				\
		  count++;								\
	   }										\
	};
	
	ADD_PIECE(plist[wpawn],_WPAWN);
	ADD_PIECE(plist[wknight],_WKNIGHT);
	ADD_PIECE(plist[wbishop],_WBISHOP);
	ADD_PIECE(plist[wrook],_WROOK);
	ADD_PIECE(plist[wqueen],_WQUEEN);
	ADD_PIECE(plist[bpawn],_BPAWN);
	ADD_PIECE(plist[bknight],_BKNIGHT);
	ADD_PIECE(plist[bbishop],_BBISHOP);
	ADD_PIECE(plist[brook],_BROOK);
	ADD_PIECE(plist[bqueen],_BQUEEN);
	score = probe_egbb(player,SQ8864(plist[wking]->sq),SQ8864(plist[bking]->sq),
		piece[0],square[0],piece[1],square[1],piece[2],square[2]);
	
	if(score != _NOTFOUND) {
		return true;
	}
#endif

    return false;
}
