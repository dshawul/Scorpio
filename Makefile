############################
# Target executable and files
############################
EXE = scorpio
RM = rm -rf
OBJ = attack.o scorpio.o eval.o hash.o moves.o probe.o search.o see.o magics.o util.o
HPP = scorpio.h my_types.h

################################################################
#
# Some Options
#  DHAS_POPCNT  --  Use Intrinsic PopCnt 
#  DMAX_CPUS=n  --  Compile for maximum of n cpus. Default is 8
#
################################################################
DEFINES = 
#DEFINES += -DHAS_POPCNT
#DEFINES += -DMAX_CPUS=16

######################
# Rules
######################

default:
	$(MAKE) gcc

clean:
	$(RM) $(OBJ) $(EXE)

strip:
	strip $(EXE)

gcc:   
	$(MAKE) \
	CXX=g++ \
	CXXFLAGS="-O3 -msse -Wall -fomit-frame-pointer -fstrict-aliasing -fno-exceptions -fno-rtti" \
	LXXFLAGS="-lm -lpthread -ldl" \
	all

cluster:   
	$(MAKE) \
	CXX=mpiCC \
	DEFINES+=-DCLUSTER \
	CXXFLAGS="-O3 -msse -Wall -fomit-frame-pointer -fstrict-aliasing -fno-exceptions -fno-rtti" \
	LXXFLAGS="-lm -lpthread -ldl" \
	all

gcc-profile:
	$(MAKE) \
	CXX=g++ \
	CXXFLAGS="-pg -O3 -msse -Wall" \
	LXXFLAGS="-pg -lm -lpthread -ldl" \
	all

all : $(EXE)
	
help:
	@echo ""
	@echo "make                  --> gcc compiler"            
	@echo "make gcc-profile      --> gcc profile with -pg switch on"
	@echo "make strip            --> remove debug information."
	@echo "make clean            --> remove intermediate files"
	@echo ""

##############
# Dependencies
############## 

$(EXE) : $(OBJ)
	$(CXX) $(LXXFLAGS) $(DEFINES) -o $@ $(OBJ)

%.o : %.cpp $(HPP)
	$(CXX) $(CXXFLAGS) $(DEFINES) -c -o $@ $<
