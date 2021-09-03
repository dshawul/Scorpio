############################
# Target executable and files
############################
BIN = ../bin
EXE = $(BIN)/scorpio
RM = rm -rf
OBJ = attack.o scorpio.o eval.o hash.o moves.o parallel.o probe.o search.o mcts.o see.o magics.o util.o
HPP = scorpio.h my_types.h params.h

#########################################################################
#
# Some options
# ------------
#
#  DARC_64BIT      --  Is system 64 bit.
#  DHAS_POPCNT     --  Use Intrinsic PopCnt. [HIGHLY recommeneded] 
#  DHAS_PREFETCH   --  Prefetch hash table entries [Recommeneded] 
#  DUSE_SPINLOCK   --  Use spin locks.
#  DCLUSTER        --  Compile MPI cluster code.
#  DCLUSTER_TT_TYPE=n  --  Distributed transposition table type (0 - global, 1 - distributed, 2 - local) default=2
#  DMAX_CPUS=n     --  Compile for maximum of n cpus. Default is 32                 
#  DYBW            --  Compile with YBW
#  DTUNE           --  Compile evaluation tuning code. [NOT recommended to turn on]
#  DMYDEBUG        --  Turn on some MPI debugging
#  DLOG_FILE       --  Include logging code
#  DBOOK_PROBE     --  Include book probing code
#  DBOOK_CREATE    --  Include book creation code
#  DEGBB           --  Include EGBB probing code
#########################################################################
DEFINES = 
DEFINES += -DARC_64BIT
DEFINES += -DHAS_POPCNT
ifneq ($(COMP),arm)
    DEFINES += -DHAS_PREFETCH
    DEFINES += -DUSE_SPINLOCK
endif
DEFINES += -DLOG_FILE
DEFINES += -DBOOK_PROBE
DEFINES += -DBOOK_CREATE
DEFINES += -DEGBB
#DEFINES += -DMYDEBUG
#DEFINES += -DTUNE
DEFINES += -DMAX_CPUS=432
#DEFINES += -DYBW
#DEFINES += -DCLUSTER_TT_TYPE=1
DEFINES += -DNNUE_INC
############################
# Compiler choice 
############################
DEBUG=0
COMP=gcc
STRIP=strip

ifeq ($(COMP),gcc-cluster)
	CXX=mpicxx -cxx=g++
	override COMP=gcc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),clang-cluster)
	CXX=mpicxx -cxx=clang++
	override COMP=clang
	DEFINES += -DCLUSTER
else ifeq ($(COMP),icpc-cluster)
	CXX=mpicxx -cxx=icpc
	override COMP=icpc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),pgcc-cluster)
	CXX=mpicxx -cxx=pgc++
	override COMP=pgcc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),win-cluster)
	CXX=mpicxx -cxx=x86_64-w64-mingw32-g++
	override COMP=win
	DEFINES += -DCLUSTER
	STRIP=x86_64-w64-mingw32-strip
else ifeq ($(COMP),gcc)
	CXX=g++
else ifeq ($(COMP),clang)
	CXX=clang++
else ifeq ($(COMP),icpc)
	CXX=icpc
else ifeq ($(COMP),pgcc)
	CXX=pgc++
else ifeq ($(COMP),arm)
	CXX=arm-linux-androideabi-g++
	STRIP=arm-linux-androideabi-strip
else ifeq ($(COMP),win)
	CXX=x86_64-w64-mingw32-g++
	STRIP=x86_64-w64-mingw32-strip
endif

STRIP += $(EXE)

###########################
#  Compiler flags
###########################

ifeq ($(COMP),arm)
    LXXFLAGS = -lm
else
    LXXFLAGS = -lpthread -lm
endif

UNUSED = -Wno-unused-variable -Wno-unused-result  -Wno-unused-result 
ifeq ($(COMP),clang)
else
    UNUSED += -Wno-unused-but-set-variable -Wno-maybe-uninitialized -Wno-psabi
endif

ifeq ($(COMP),win)
    LXXFLAGS += -static
    CXXFLAGS = -Wall -fstrict-aliasing -fno-exceptions -fno-rtti $(UNUSED) -std=c++11
else ifeq ($(COMP),pgcc)
    CXXFLAGS = warn -c11
    LXXFLAGS += -ldl
else ifeq ($(COMP),icpc)
    CXXFLAGS  = -wd128 -wd981 -wd869 -wd2259 -wd383 -wd1418
    CXXFLAGS += -fstrict-aliasing -fno-exceptions -fno-rtti $(UNUSED) -std=c++11
    LXXFLAGS += -ldl
else
    CXXFLAGS = -Wall -fstrict-aliasing -fno-exceptions -fno-rtti $(UNUSED) -std=c++11
    LXXFLAGS += -ldl
endif

ifeq ($(COMP),arm)
     CXXFLAGS += -fPIE -pie
     LXXFLAGS += -fPIE -pie
else ifeq ($(COMP),pgcc)
     CXXFLAGS += -Mvect=avx2
else
     CXXFLAGS += -mavx2
endif

ifeq ($(DEBUG),3)
        ifeq ($(COMP),icpc)
                CXXFLAGS += -O2 -prof-gen
        else ifeq ($(COMP),pgcc)
                CXXFLAGS += -O2 -Mpfi
        else ifeq ($(COMP),clang)
                CXXFLAGS += -O2 -fprofile-instr-generate
                LXXFLAGS += -fprofile-instr-generate
        else
                CXXFLAGS += -O2 -fprofile-generate
                LXXFLAGS += -lgcov
        endif
        STRIP=
else ifeq ($(DEBUG),2)
        ifeq ($(COMP),icpc)
                CXXFLAGS += -g -pg
        else
                CXXFLAGS += -g -pg
                LXXFLAGS += -g -pg
        endif
        STRIP=
else ifeq ($(DEBUG),1)
        ifeq ($(COMP),icpc)
                CXXFLAGS += -prof-use -fast -fomit-frame-pointer
        else ifeq ($(COMP),pgcc)
                CXXFLAGS += -Mpfo -fast noframe
        else ifeq ($(COMP),clang)
                CXXFLAGS += -fprofile-instr-use -Ofast -fomit-frame-pointer -flto
                LXXFLAGS += -fprofile-use -flto
        else
                CXXFLAGS += -fprofile-use -Ofast -fomit-frame-pointer -flto -Wno-error=coverage-mismatch
                LXXFLAGS += -lgcov -flto
        endif
else
        ifeq ($(COMP),icpc)
                CXXFLAGS += -fast -fomit-frame-pointer
        else ifeq ($(COMP),pgcc)
                CXXFLAGS += -fast noframe
        else
                CXXFLAGS += -Ofast -fomit-frame-pointer -flto
                LXXFLAGS += -flto
        endif
endif

######################
# Rules
######################

default:
	$(MAKE) $(EXE) strip

profile:
	$(MAKE) DEBUG=3 $(EXE)
	$(BIN)/scorpio.sh mt 1 use_nn 0 montecarlo 0 st 20 go quit
	@if [ $(COMP) = clang ]; then \
	    llvm-profdata merge -output=default.profdata $(BIN)/*.profraw; \
	fi
	$(RM) $(OBJ) $(EXE) $(BIN)/*.profraw
	$(MAKE) DEBUG=1 $(EXE)

clean:
	$(RM) $(OBJ) $(EXE) $(BIN)/core.* $(BIN)/*.profraw *.profdata *.gcda

strip:
	$(STRIP)

help:
	@echo ""
	@echo "1. make [DEBUG=n] [COMP=c]"
	@echo ""
	@echo "  n ="
	@echo "	0: Compile optimized binary (-03)"
	@echo "	1: Compile with profile guided optimization (PGO)"
	@echo "	2: Compile for deugging (default)"
	@echo "	3: Prepare for PGO"
	@echo ""
	@echo "  c ="
	@echo "	gcc    :  gnu compiler"
	@echo "	clang  :  clang compiler"
	@echo "	icpc   :  intel compiler"
	@echo "	pgcc   :  portland group compiler"
	@echo "	arm    :  arm android compiler"
	@echo "	win    :  windows cross compiler"
	@echo "	[]-cluster:  mpicxx wrapper compiler"
	@echo ""
	@echo "2. make clean - removes all files but source code"
	@echo "3. make strip - strips executable of debugging/profiling data"
	@echo ""

##############
# Dependencies
############## 

$(EXE): $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LXXFLAGS) 

%.o: %.cpp $(HPP)
	$(CXX) $(CXXFLAGS) $(DEFINES) -c -o $@ $<
