############################
# Scorpio Chess Engine Makefile
############################
.PHONY: default all clean strip help profile install

# Target executable and directories
BIN = ../bin
EXE = $(BIN)/scorpio
OBJ_DIR = ./obj

# File management
RM = rm -rf
MKDIR = mkdir -p

# Source files and dependencies
SOURCES = attack.cpp scorpio.cpp eval.cpp hash.cpp moves.cpp parallel.cpp probe.cpp search.cpp mcts.cpp see.cpp magics.cpp util.cpp
OBJECTS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SOURCES))
HEADERS = scorpio.h my_types.h params.h

# Build configurations
DEBUG ?= 0
COMP ?= gcc
STRIP_CMD = strip

#########################################################################
# Feature Options
#########################################################################
# DARC_64BIT      -- Is system 64 bit
# DHAS_POPCNT     -- Use intrinsic PopCnt [HIGHLY recommended]
# DHAS_PREFETCH   -- Prefetch hash table entries [Recommended]
# DUSE_SPINLOCK   -- Use spin locks
# DCLUSTER        -- Compile MPI cluster code
# DCLUSTER_TT_TYPE=n -- Distributed transposition table type
#                    (0 - global, 1 - distributed, 2 - local) default=2
# DYBW            -- Compile with YBW
# DTUNE           -- Compile evaluation tuning code [NOT recommended]
# DMYDEBUG        -- Turn on some MPI debugging
# DNNUE_INC       -- Include NNUE support
#########################################################################

# Architecture-specific options
ARCH_DEFINES = -DARC_64BIT -DHAS_POPCNT

# Feature options
FEATURE_DEFINES = -DNNUE_INC

# Conditional options based on compiler
CONDITIONAL_DEFINES = 
ifneq ($(COMP),arm)
    CONDITIONAL_DEFINES += -DHAS_PREFETCH -DUSE_SPINLOCK
endif

# Debug options
DEBUG_DEFINES =
#DEBUG_DEFINES += -DMYDEBUG

# Optional features
OPTIONAL_DEFINES =
#OPTIONAL_DEFINES += -DTUNE
#OPTIONAL_DEFINES += -DYBW
#OPTIONAL_DEFINES += -DCLUSTER_TT_TYPE=1

# Combine all defines
DEFINES = $(ARCH_DEFINES) $(FEATURE_DEFINES) $(CONDITIONAL_DEFINES) $(DEBUG_DEFINES) $(OPTIONAL_DEFINES)

############################
# Compiler settings
############################

# C++ standard
CXXSTANDARD = -std=c++17

# Warning flags
WARNINGS = -Wall -fstrict-aliasing -fno-exceptions -fno-rtti

# Unused warnings suppression
UNUSED = -Wno-unused-variable -Wno-unused-result -Wno-unused-but-set-variable -Wno-psabi -Wno-uninitialized

# Default flags
CXXFLAGS =
LDFLAGS =

############################
# Compiler choices
############################
# Setup compiler based on COMP variable
ifeq ($(COMP),gcc-cluster)
	CXX = mpicxx -cxx=g++
	override COMP = gcc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),clang-cluster)
	CXX = mpicxx -cxx=clang++
	override COMP = clang
	DEFINES += -DCLUSTER
else ifeq ($(COMP),icpc-cluster)
	CXX = mpicxx -cxx=icpc
	override COMP = icpc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),pgcc-cluster)
	CXX = mpicxx
	override COMP = pgcc
	DEFINES += -DCLUSTER
else ifeq ($(COMP),win-cluster)
	CXX = x86_64-w64-mingw32-g++
	override COMP = win
	DEFINES += -DCLUSTER
	STRIP_CMD = x86_64-w64-mingw32-strip
	LDFLAGS += -L/usr/local/msmpi/Lib/x64 -lmsmpi
	CXXFLAGS += -I/usr/local/msmpi/Include
else ifeq ($(COMP),gcc)
	CXX = g++
else ifeq ($(COMP),clang)
	CXX = clang++
else ifeq ($(COMP),icpc)
	CXX = icpc
else ifeq ($(COMP),pgcc)
	CXX = pgc++
else ifeq ($(COMP),arm)
	CXX = aarch64-linux-android21-clang++
	STRIP_CMD =llvm-strip
else ifeq ($(COMP),win)
	CXX = x86_64-w64-mingw32-g++
	STRIP_CMD = x86_64-w64-mingw32-strip
endif

# Threading support
ifeq ($(COMP),arm)
	LDFLAGS += -lm
else
	LDFLAGS += -lpthread -lm
endif

# Compiler-specific options
ifeq ($(COMP),win)
	LDFLAGS += -static
	CXXFLAGS += $(WARNINGS) $(UNUSED) $(CXXSTANDARD)
else ifeq ($(COMP),pgcc)
	CXXFLAGS += -Minform=inform --diag_suppress integer_sign_change,set_but_not_used,declared_but_not_referenced
	CXXFLAGS += $(CXXSTANDARD)
	LDFLAGS += -ldl
else ifeq ($(COMP),icpc)
	CXXFLAGS += -wd128 -wd981 -wd869 -wd2259 -wd383 -wd1418
	CXXFLAGS += $(WARNINGS) $(UNUSED) $(CXXSTANDARD)
	LDFLAGS += -ldl
else
	CXXFLAGS += $(WARNINGS) $(UNUSED) $(CXXSTANDARD)
	LDFLAGS += -ldl
endif

# Architecture-specific optimizations
ifeq ($(COMP),arm)
	CXXFLAGS += -fPIE
	LDFLAGS += -fPIE -pie
else ifeq ($(COMP),pgcc)
	ifneq ($(USE_AVX2),0)
		CXXFLAGS += -tp=haswell
	endif
else
	ifneq ($(USE_AVX2),0)
		CXXFLAGS += -mavx2
	endif
endif

############################
# Debug/Optimization settings
############################
ifeq ($(DEBUG),3)
	# Prepare for profile guided optimization
	ifeq ($(COMP),icpc)
		CXXFLAGS += -O2 -prof-gen
	else ifeq ($(COMP),pgcc)
		CXXFLAGS += -O2 -Mpfi
	else ifeq ($(COMP),clang)
		CXXFLAGS += -O2 -fprofile-instr-generate
		LDFLAGS += -fprofile-instr-generate
	else
		CXXFLAGS += -O2 -fprofile-generate
		LDFLAGS += -lgcov
	endif
	DO_STRIP = 0
else ifeq ($(DEBUG),2)
	# Debug build with profiling
	ifeq ($(COMP),icpc)
		CXXFLAGS += -g -pg
	else
		CXXFLAGS += -g -pg
		LDFLAGS += -g -pg
	endif
	DO_STRIP = 0
else ifeq ($(DEBUG),1)
	# Optimized build with profile data
	ifeq ($(COMP),icpc)
		CXXFLAGS += -prof-use -fast -fomit-frame-pointer
	else ifeq ($(COMP),pgcc)
		CXXFLAGS += -Mpfo -fast noframe
	else ifeq ($(COMP),clang)
		CXXFLAGS += -fprofile-instr-use -Ofast -fomit-frame-pointer -flto
		LDFLAGS += -fprofile-use -flto
	else
		CXXFLAGS += -fprofile-use -Ofast -fomit-frame-pointer -flto -Wno-coverage-mismatch
		LDFLAGS += -lgcov -flto
	endif
	DO_STRIP = 1
else
	# Default optimized build
	ifeq ($(COMP),icpc)
		CXXFLAGS += -fast -fomit-frame-pointer
	else ifeq ($(COMP),pgcc)
		CXXFLAGS += -fast noframe
	else
		CXXFLAGS += -Ofast -fomit-frame-pointer -flto
		LDFLAGS += -flto
	endif
	DO_STRIP = 1
endif

######################
# Rules
######################

default: all

all: $(OBJ_DIR) $(EXE)
	@if [ "$(DO_STRIP)" = "1" ]; then $(MAKE) strip; fi

# Create necessary directories
$(OBJ_DIR):
	$(MKDIR) $(OBJ_DIR)

# Main executable
$(EXE): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Automatic dependency generation
DEPFLAGS = -MMD -MP
$(OBJ_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(DEPFLAGS) -c -o $@ $<

# Include generated dependencies
-include $(OBJECTS:.o=.d)

# Profile guided optimization workflow
PROFILE_PARAMS ?= mt 1 use_nn 0 use_nnue 0 montecarlo 0 st 20 go quit
profile: $(OBJ_DIR)
	$(MAKE) DEBUG=3 $(EXE)
	$(BIN)/scorpio.sh $(PROFILE_PARAMS)
	@if [ "$(COMP)" = "clang" ]; then \
		llvm-profdata merge -output=default.profdata $(BIN)/*.profraw; \
	fi
	$(RM) $(OBJECTS) $(EXE) $(BIN)/*.profraw
	$(MAKE) DEBUG=1 $(EXE)

# Strip debugging symbols
strip:
	@if [ "$(COMP)" = "win" ]; then \
	    $(STRIP_CMD) $(EXE).exe; \
	else \
	    $(STRIP_CMD) $(EXE); \
	fi

# Clean build artifacts
clean:
	$(RM) $(OBJ_DIR) $(EXE) $(BIN)/core.* *.profdata $(BIN)/*.profraw

# Help information
help:
	@echo ""
	@echo "Scorpio Chess Engine Makefile"
	@echo "============================="
	@echo ""
	@echo "Available targets:"
	@echo "  all      : Build the Scorpio executable (default)"
	@echo "  clean    : Remove all build artifacts"
	@echo "  strip    : Strip debugging symbols from the executable"
	@echo "  profile  : Build with profile-guided optimization"
	@echo "  help     : Display this help message"
	@echo ""
	@echo "Build options:"
	@echo "  DEBUG=n  : Set debug level"
	@echo "    0: Optimized build (default)"
	@echo "    1: Build with profile-guided optimization"
	@echo "    2: Debug build with profiling"
	@echo "    3: Prepare for profile-guided optimization"
	@echo ""
	@echo "  COMP=c   : Set compiler"
	@echo "    gcc          : GNU C++ compiler (default)"
	@echo "    clang        : Clang C++ compiler"
	@echo "    icpc         : Intel C++ compiler"
	@echo "    pgcc         : Portland Group compiler"
	@echo "    arm          : ARM Android compiler"
	@echo "    win          : Windows cross-compiler"
	@echo "    *-cluster    : MPI wrapper for cluster compilation"
	@echo ""
	@echo "  USE_AVX2=0     : Disable AVX2 instructions"
	@echo "  PROFILE_PARAMS : Parameters for profile generation"
	@echo ""
