############################
# Target executable and files
############################
EXE = scorpio
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
#  DPARALLEL       --  Compile parallel search code.
#  DUSE_SPINLOCK   --  Use spin locks.
#  DCLUSTER        --  Compile MPI cluster code.
#  DMAX_CPUS=n     --  Compile for maximum of n cpus. Default is 32                 
#  DSMP_TT_TYPE=n  --  Shared memory transposition table type (0 - global, 1 - distributed, 2 - local) default=0
#  DDST_TT_TYPE=n  --  Distributed   transposition table type (0 - global, 1 - distributed, 2 - local) default=1
#  DTUNE           --  Compile evaluation tuning code. [NOT recommended to turn on]
#  DTHREAD_POLLING --  Poll for MPI message using a thread. 
#  DMYDEBUG        --  Turn on some MPI debugging
#  DLOG_FILE       --  Include logging code
#  DBOOK_PROBE     --  Include book probing code
#  DBOOK_CREATE    --  Include book creation code
#  DEGBB           --  Include EGBB probing code
#########################################################################
DEFINES = 
DEFINES += -DARC_64BIT
DEFINES += -DHAS_POPCNT
DEFINES += -DHAS_PREFETCH
DEFINES += -DPARALLEL
DEFINES += -DUSE_SPINLOCK
DEFINES += -DTHREAD_POLLING
DEFINES += -DLOG_FILE
DEFINES += -DBOOK_PROBE
DEFINES += -DBOOK_CREATE
DEFINES += -DEGBB
#DEFINES += -DMYDEBUG
#DEFINES += -DTUNE
#DEFINES += -DMAX_CPUS=256
#DEFINES += -DNUMA_TT_TYPE=0
#DEFINES += -DCLUSTER_TT_TYPE=1
############################
# Compiler choice 
############################
DEBUG=0
COMP=gcc
STRIP=strip

ifeq ($(COMP),gcc-cluster)
	CXX="mpic++"
	override COMP=gcc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),icpc-cluster)
	CXX="mpic++"
	override COMP=icpc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),pgcc-cluster)
	CXX="mpic++"
	override COMP=pgcc
else ifeq ($(COMP),pgcc)
	CXX=pgc++
else ifeq ($(COMP),gcc)
	CXX=g++
else ifeq ($(COMP),clang)
	CXX=clang++
else ifeq ($(COMP),icpc)
	CXX=icpc
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

LXXFLAGS = -lpthread -lm

ifeq ($(COMP),win)
    LXXFLAGS += -static
    CXXFLAGS = -Wall -fstrict-aliasing -fno-exceptions -fno-rtti -Wno-unused-result -msse
else ifeq ($(COMP),pgcc)
    CXXFLAGS = warn -Mvect=sse
    LXXFLAGS += -ldl
else ifeq ($(COMP),icpc)
    CXXFLAGS  = -wd128 -wd981 -wd869 -wd2259 -wd383 -wd1418
    CXXFLAGS += -fstrict-aliasing -fno-exceptions -fno-rtti -Wno-unused-result -msse
    LXXFLAGS += -ldl
else
    CXXFLAGS = -Wall -fstrict-aliasing -fno-exceptions -fno-rtti -Wno-unused-result -msse
    LXXFLAGS += -ldl
endif

ifeq ($(DEBUG),3)
        ifeq ($(COMP),icpc)
                CXXFLAGS += -O2 -prof-gen
        else ifeq ($(COMP),pgcc)
                CXXFLAGS += -O2 -Mpfi
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
        else
                CXXFLAGS += -fprofile-use -Ofast -fomit-frame-pointer -flto
                LXXFLAGS += -lgcov
        endif
else
        ifeq ($(COMP),icpc)
                CXXFLAGS += -fast -fomit-frame-pointer
        else ifeq ($(COMP),pgcc)
                CXXFLAGS += -fast noframe
        else
                CXXFLAGS += -Ofast -fomit-frame-pointer
                ifneq ($(COMP),clang)
                    CXXFLAGS += -flto
                endif
        endif
endif

######################
# Rules
######################

default:
	$(MAKE) $(EXE) strip

clean:
	$(RM) $(OBJ) $(EXE) core.* cluster.* *.gcda

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
	@echo "	gcc    :  g++ compiler"
	@echo "	icpc   :  intel compiler"
	@echo "	arm    :  arm android compiler"
	@echo "	cluster:  mpiC++ compiler"
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
