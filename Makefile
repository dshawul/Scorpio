############################
# Target executable and files
############################
EXE = scorpio
RM = rm -rf
OBJ = attack.o scorpio.o eval.o hash.o moves.o parallel.o probe.o search.o see.o magics.o util.o
HPP = scorpio.h my_types.h
ICPC_WARN = -wd128 -wd981 -wd869 -wd2259 -wd383 -wd1418

#########################################################################
#
# Some Options : 
#  DHAS_POPCNT  --  Use Intrinsic PopCnt. [HIGHLY recommeneded] 
#  DMAX_CPUS=n  --  Compile for maximum of n cpus. Default is 8
#  DMAX_HOSTS=n --  Compile for maximum of n cpus in a cluster.
#                   Default value is 128.
#  DTUNE        --  Compile evaluation tuning code. [NOT recommended]
#  DTT_TYPE=n   --  Transposition table type for NUMA (0 - global, 1 - distributed, 2 - local)
#########################################################################
DEFINES = 
#DEFINES += -DHAS_POPCNT
DEFINES += -DMAX_CPUS=32
#DEFINES += -DMAX_HOSTS=128
#DEFINES += -DTUNE
#DEFINES += -DTT_TYPE=1

######################
# Rules
######################

default:
	$(MAKE) gcc

clean:
	$(RM) $(OBJ) $(EXE) core.* cluster.*

strip:
	strip $(EXE)

arm-strip:
	arm-linux-androideabi-strip $(EXE)

gcc:   
	$(MAKE) \
	CXX=g++ \
	CXXFLAGS="-O3 -Wall -fomit-frame-pointer -fstrict-aliasing -fno-exceptions -fno-rtti" \
	LXXFLAGS="-lm -lpthread -ldl" \
	all

cluster:   
	$(MAKE) \
	CXX=mpiCC \
	DEFINES="$(DEFINES) -DCLUSTER " \
	CXXFLAGS="-O3 -Wall -fomit-frame-pointer -fstrict-aliasing -fno-exceptions -fno-rtti $(ICPC_WARN) " \
	LXXFLAGS="-lm -lpthread -ldl" \
	all

gcc-profile:
	$(MAKE) \
	CXX=g++ \
	CXXFLAGS="-pg -O3 -msse -Wall" \
	LXXFLAGS="-pg -lm -lpthread -ldl" \
	all

cluster-profile:   
	$(MAKE) \
	CXX=mpiCC \
	DEFINES="$(DEFINES) -DCLUSTER " \
	CXXFLAGS="-pg -O3 -Wall $(ICPC_WARN) " \
	LXXFLAGS="-pg -lm -lpthread -ldl" \
	all

arm:   
	$(MAKE) \
	CXX=arm-linux-androideabi-g++ \
	CXXFLAGS="-O3 -Wall -fomit-frame-pointer -fstrict-aliasing -fno-exceptions -fno-rtti" \
	LXXFLAGS="-lm -ldl" \
	all

all: $(EXE)
	
help:
	@echo ""
	@echo "make gcc              --> gcc compiler"            
	@echo "make gcc-profile      --> profile with gcc"
	@echo "make cluster          --> compile cluster version"     
	@echo "make cluster-profile  --> profile cluster version"     
	@echo "make strip            --> remove debug information."
	@echo "make clean            --> remove intermediate files"
	@echo ""

##############
# Dependencies
############## 

$(EXE): $(OBJ)
	$(CXX) $(LXXFLAGS) $(DEFINES) -o $@ $(OBJ)

%.o: %.cpp $(HPP)
	$(CXX) $(CXXFLAGS) $(DEFINES) -c -o $@ $<
