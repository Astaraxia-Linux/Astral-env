.POSIX:

BUILD_DIR := .build
OUT := astral-env
SNAPD := astral-env-snapd

CXX = g++

CXXFLAGS += -Wall
CXXFLAGS += -std=c++23 -O2 -g

CXXFLAGS += -iquote .

LIBS += libcurl openssl

LDLIBS += $(shell pkg-config --libs $(LIBS))
LDLIBS += 

VPATH += src
SRC := $(shell find . -name "*.cpp")

vpath %.cpp $(VPATH)

OBJ := $(SRC:%.cpp=$(BUILD_DIR)/%.o)
MAIN_OBJ := $(filter-out $(BUILD_DIR)/snapd/snapd.o, $(OBJ))
SNAPD_OBJ := $(filter-out $(BUILD_DIR)/main.o, $(OBJ))

.PHONY: all
all: $(OUT) $(SNAPD)

$(BUILD_DIR)/%.o: %.cpp
	@ mkdir -p $(dir $@)
	$Q $(CXX) $(CXXFLAGS) -o $@ -c $<
	@ $(LOG_TIME) "CXX $(C_PURPLE) $(notdir $@) $(C_RESET)"

$(OUT): $(filter-out $(BUILD_DIR)/snapd/snapd.o, $(OBJ))
	@ mkdir -p $(dir $@)
	$Q $(CXX) -o $@ $(filter-out $(BUILD_DIR)/snapd/snapd.o, $(OBJ)) $(CXXFLAGS) $(LDLIBS) $(LDFLAGS)
	@ $(LOG_TIME) "LD $(C_GREEN) $@ $(C_RESET)"

$(SNAPD): $(SNAPD_OBJ)
	@ mkdir -p $(dir $@)
	$Q $(CXX) -o $@ $(SNAPD_OBJ) $(CXXFLAGS) $(LDLIBS) $(LDFLAGS)
	@ $(LOG_TIME) "LD $(C_GREEN) $@ $(C_RESET)"

.PHONY: clean
clean:
	$(RM) $(OBJ)
	@ $(LOG_TIME) $@

.PHONY: fclean
fclean: clean
	$(RM) -r $(BUILD_DIR) $(OUT)
	@ $(LOG_TIME) $@

.PHONY: re
.NOTPARALLEL: re
re: fclean all

PREFIX ?= /usr/bin

.PHONY: install
install:
	install -Dm0755 $(OUT) -t $(PREFIX)
	install -Dm0755 $(SNAPD) -t $(PREFIX)

ifneq ($(shell command -v tput),)
  ifneq ($(shell tput colors),0)

C_RESET := \033[00m
C_BOLD := \e[1m
C_RED := \e[31m
C_GREEN := \e[32m
C_YELLOW := \e[33m
C_BLUE := \e[34m
C_PURPLE := \e[35m
C_CYAN := \e[36m

C_BEGIN := \033[A

  endif
endif

NOW = $(shell date +%s%3N)

STIME := $(shell date +%s%3N)
export STIME

define TIME_MS
$$( expr \( $$(date +%s%3N) - $(STIME) \))
endef

BOXIFY = "[$(C_BLUE)$(1)$(C_RESET)] $(2)"

ifneq ($(shell command -v printf),)
  LOG_TIME = printf $(call BOXIFY, %6s , %b\n) "$(call TIME_MS)"
else
  LOG_TIME = echo -e $(call BOXIFY, $(call TIME_MS) ,)
endif
