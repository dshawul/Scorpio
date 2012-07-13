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
#  DHAS_POPCNT   --  Use Intrinsic PopCnt. [HIGHLY recommeneded] 
#  DHAS_PREFETCH --  Prefetch hash table entries [Recommeneded] 
#  DMAX_CPUS=n   --  Compile for maximum of n cpus. Default is 8
#  DMAX_HOSTS=n  --  Compile for maximum of n cpus in a cluster.
#                    Default value is 128.
#  DTUNE         --  Compile evaluation tuning code. [NOT recommended]
#  DTT_TYPE=n    --  Transposition table type for NUMA (0 - global, 1 - distributed, 2 - local)
#########################################################################
DEFINES = 
DEFINES += -DHAS_POPCNT
DEFINES += -DHAS_PREFETCH
DEFINES += -DMAX_CPUS=32
#DEFINES += -DMAX_HOSTS=128
#DEFINES += -DTUNE
#DEFINES += -DTT_TYPE=1

############################
# Compiler
#   gcc      --> g++
#   icpc     --> icpc
#   arm      --> arm-linux-androideabi
#   cluster  --> mpiCC
# Debug
#   0        --> none
#   1        --> -g
#   2        --> -pg
############################

COMPILER=gcc
DEBUG=0

CXXFLAGS = -O3 -Wall -fstrict-aliasing -fno-exceptions -fno-rtti
LXXFLAGS = -lm -ldl

ifeq ($(DEBUG),0)
	CXXFLAGS += -fomit-frame-pointer 
else
	ifeq ($(DEBUG),1)
		CXXFLAGS += -g
		LXXFLAGS += -g
	else
		CXXFLAGS += -g -pg
		LXXFLAGS += -g -pg
	endif
endif

STRIP=strip

ifeq ($(COMPILER),gcc)
	CXX=g++
	CXXFLAGS += -msse
	LXXFLAGS += -lpthread
else ifeq ($(COMPILER),icpc)
	CXX=icpc
	CXXFLAGS += -wd128 -wd981 -wd869 -wd2259 -wd383 -wd1418 -vec_report0
	LXXFLAGS += -lpthread
else ifeq ($(COMPILER),arm)
	CXX=arm-linux-androideabi-g++
	STRIP=arm-linux-androideabi-strip 
else ifeq ($(COMPILER),cluster)
	CXX="mpiCC"
	CXXFLAGS += -wd128 -wd981 -wd869 -wd2259 -wd383 -wd1418
	LXXFLAGS += -lpthread
	DEFINES += -DCLUSTER
endif

STRIP += $(EXE)

ifneq ($(DEBUG),0)
	STRIP=
endif
######################
# Rules
######################

default:
	$(MAKE) $(EXE) strip

clean:
	$(RM) $(OBJ) $(EXE) core.* cluster.*

strip:
	$(STRIP)

##############
# Dependencies
############## 

$(EXE): $(OBJ)
	$(CXX) $(LXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp $(HPP)
	$(CXX) $(CXXFLAGS) $(DEFINES) -c -o $@ $<
