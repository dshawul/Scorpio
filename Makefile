############################
# Target executable and files
############################
EXE = scorpio
RM = rm -rf
OBJ = attack.o scorpio.o eval.o hash.o moves.o parallel.o probe.o search.o see.o magics.o util.o
HPP = scorpio.h my_types.h

#########################################################################
#
# Some Options : 
#  DARC_64BIT    --  Is system 64 bit.
#  DHAS_POPCNT   --  Use Intrinsic PopCnt. [HIGHLY recommeneded] 
#  DHAS_PREFETCH --  Prefetch hash table entries [Recommeneded] 
#  DPARALLEL     --  Compile parallel search code.
#  DMAX_CPUS=n   --  Compile for maximum of n cpus. Default is 32
#  DMAX_HOSTS=n  --  Compile for maximum of n cpus in a cluster. Default value is 128.                   
#  DTT_TYPE=n    --  Transposition table type for NUMA (0 - global, 1 - distributed, 2 - local)
#  DTUNE         --  Compile evaluation tuning code. [NOT recommended]
#
#########################################################################
DEFINES = 
#DEFINES += -DARC_64BIT
#DEFINES += -DHAS_POPCNT
#DEFINES += -DHAS_PREFETCH
#DEFINES += -DPARALLEL
#DEFINES += -DMAX_CPUS=64
#DEFINES += -DMAX_HOSTS=128
#DEFINES += -DTUNE
#DEFINES += -DTT_TYPE=1
DEFINES += -DTHREAD_POLLING

############################
# Compiler choice 
############################
DEBUG=0
COMP=gcc-cluster
STRIP=strip

ifeq ($(COMP),gcc-cluster)
	CXX="mpic++"
	override COMP=gcc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),icpc-cluster)
	CXX="mpic++"
	override COMP=icpc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),gcc)
	CXX=g++
else ifeq ($(COMP),icpc)
	CXX=icpc
else ifeq ($(COMP),arm)
	CXX=arm-linux-androideabi-g++
	STRIP=arm-linux-androideabi-strip
endif

STRIP += $(EXE)

###########################
#  Compiler flags
###########################
CXXFLAGS = -Wall -fstrict-aliasing -fno-exceptions -fno-rtti
LXXFLAGS = -lm -ldl

ifeq ($(COMP),gcc)
	CXXFLAGS += -msse
	LXXFLAGS += -lpthread
else ifeq ($(COMP),icpc)
	CXXFLAGS += -wd128 -wd981 -wd869 -wd2259 -wd383 -wd1418
	LXXFLAGS += -lpthread
endif

ifeq ($(DEBUG),3)
	ifeq ($(COMP),icpc)
		CXXFLAGS += -O2 -prof-gen
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
	else
		CXXFLAGS += -fprofile-use -Ofast -fomit-frame-pointer -flto
		LXXFLAGS += -lgcov
	endif
else
	ifeq ($(COMP),icpc)
		CXXFLAGS += -fast -fomit-frame-pointer
	else
		CXXFLAGS += -Ofast -fomit-frame-pointer -flto
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
