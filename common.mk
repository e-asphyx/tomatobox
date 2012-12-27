CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
STRIP = $(CROSS_COMPILE)strip

CXXFLAGS ?= $(CFLAGS)
ASFLAGS ?= $(CFLAGS)

ifdef BIN
$(BIN)_CXXFLAGS ?= $($(BIN)_CFLAGS)
$(BIN)_ASFLAGS ?= $($(BIN)_CFLAGS)
endif

ifdef SHARED_LIB
$(SHARED_LIB)_CXXFLAGS ?= $($(SHARED_LIB)_CFLAGS)
$(SHARED_LIB)_ASFLAGS ?= $($(SHARED_LIB)_CFLAGS)
endif

OBJ_PREFIX = .obj
DEP_PREFIX = .dep

C_SOURCES = $(filter %.c, $(SOURCES))
CXX_SOURCES = $(filter %.cpp, $(SOURCES))
AS_SOURCES = $(filter %.s, $(SOURCES))

C_OBJECTS = $(patsubst %.c, $(OBJ_PREFIX)/%.o, $(C_SOURCES))
CXX_OBJECTS = $(patsubst %.cpp, $(OBJ_PREFIX)/%.o, $(CXX_SOURCES))
AS_OBJECTS = $(patsubst %.s, $(OBJ_PREFIX)/%.o, $(AS_SOURCES))

OBJECTS = $(C_OBJECTS) $(CXX_OBJECTS) $(AS_OBJECTS)

C_DEPS = $(patsubst %.c, $(DEP_PREFIX)/%.d, $(C_SOURCES))
CXX_DEPS = $(patsubst %.cpp, $(DEP_PREFIX)/%.d, $(CXX_SOURCES))
AS_DEPS = $(patsubst %.s, $(DEP_PREFIX)/%.d, $(AS_SOURCES))

DEPS = $(C_DEPS) $(CXX_DEPS) $(AS_DEPS)

ifneq ($(strip $(CXX_OBJECTS)),)
    LINKER=$(CXX)
else
    LINKER=$(CC)
endif

SHARED_LIB_SUFFIX ?= .so

ifdef SHARED_LIB
SHARED_LIB_LINKNAME = lib$(SHARED_LIB)$(SHARED_LIB_SUFFIX)
SHARED_LIB_NAME = $(SHARED_LIB_LINKNAME).$(SHARED_LIB_VER)
SHARED_LIB_SONAME = $(SHARED_LIB_LINKNAME).$(SHARED_LIB_SOVER)
endif

SUBDIRS_CLEAN = $(addsuffix .clean, $(SUBDIRS))
SUBDIRS_MAKE = $(addsuffix .subdir, $(SUBDIRS))

CC_AS_OPT = -x assembler-with-cpp

.PHONY: all clean strip $(SUBDIRS_MAKE) $(SUBDIRS_CLEAN)

all: $(BIN) $(SHARED_LIB_NAME)

$(SUBDIRS_MAKE):
	$(MAKE) -C $(patsubst %.subdir, %, $@)

strip: $(BIN) $(SHARED_LIB_NAME)
	$(STRIP) --strip-unneeded $<

%/.stamp:
	mkdir -p $(dir $@)
	touch $@

$(OBJECTS): $(sort $(foreach X, $(OBJECTS), $(dir $(X)).stamp))
$(DEPS): $(sort $(foreach X, $(DEPS), $(dir $(X)).stamp))

# compilation rules
$(C_OBJECTS): $(OBJ_PREFIX)/%.o: %.c $(DEP_PREFIX)/%.d
	$(CC) $(CPPFLAGS) $(CFLAGS) $($(BIN)_CFLAGS) $($(SHARED_LIB)_CFLAGS) -c $< -o $@

$(CXX_OBJECTS): $(OBJ_PREFIX)/%.o: %.cpp $(DEP_PREFIX)/%.d
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $($(BIN)_CXXFLAGS) $($(SHARED_LIB)_CXXFLAGS) -c $< -o $@

$(AS_OBJECTS): $(OBJ_PREFIX)/%.o: %.s $(DEP_PREFIX)/%.d
	$(CC) $(CC_AS_OPT) $(CPPFLAGS) $(ASFLAGS) $($(BIN)_ASFLAGS) $($(SHARED_LIB)_ASFLAGS) -c $< -o $@

# dependencies
$(C_DEPS): $(DEP_PREFIX)/%.d: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $($(BIN)_CFLAGS) $($(SHARED_LIB)_CFLAGS) -M $< -MT "$(OBJ_PREFIX)/$*.o" -o $@

$(CXX_DEPS): $(DEP_PREFIX)/%.d: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $($(BIN)_CXXFLAGS) $($(SHARED_LIB)_CXXFLAGS) -M $< -MT "$(OBJ_PREFIX)/$*.o" -o $@

$(AS_DEPS): $(DEP_PREFIX)/%.d: %.s
	$(CC) $(CC_AS_OPT) $(CPPFLAGS) $(ASFLAGS) $($(BIN)_ASFLAGS) $($(SHARED_LIB)_ASFLAGS) -M $< -MT "$(OBJ_PREFIX)/$*.o" -o $@

$(BIN): $(OBJECTS)
	$(LINKER) -o $@ $(filter-out $(SUBDIRS_MAKE), $^) $(LDFLAGS) $($(BIN)_LDFLAGS)

$(SHARED_LIB_NAME): $(OBJECTS)
	$(LINKER) -shared -Wl,-soname,$(SHARED_LIB_SONAME) -o $@ $(filter-out $(SUBDIRS_MAKE), $^) \
		$(LDFLAGS) $($(SHARED_LIB)_LDFLAGS)
	ln -sf $(@F) $(@D)/$(SHARED_LIB_SONAME)
	ln -sf $(@F) $(@D)/$(SHARED_LIB_LINKNAME)

$(BIN) $(SHARED_LIB_NAME): $(SUBDIRS_MAKE)

$(SUBDIRS_CLEAN):
	$(MAKE) -C $(patsubst %.clean, %, $@) clean

clean: $(SUBDIRS_CLEAN)
	$(RM) $(BIN) $(OBJECTS) $(DEPS) $(SHARED_LIB_NAME) $(SHARED_LIB_SONAME) $(SHARED_LIB_LINKNAME)

-include $(DEPS)
