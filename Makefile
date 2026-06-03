#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET   : name of the output (defaults to the project folder name)
# BUILD    : intermediate object directory
# SOURCES  : directories with .c/.cpp/.s source
# INCLUDES : directories with headers
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include

APP_TITLE       := Dual GBA
APP_DESCRIPTION := Two GBA games at once, one per screen
APP_AUTHOR      := Guy Shtainer

#---------------------------------------------------------------------------------
# CIA packaging (requires makerom + bannertool on PATH; see README).
# The New 3DS flags (804MHz / L2 / core 2 access) live in $(RSF).
#---------------------------------------------------------------------------------
MAKEROM          ?= makerom
BANNERTOOL       ?= bannertool

RSF              := app.rsf
BANNER_IMAGE     := banner.png
BANNER_AUDIO     := banner.wav

APP_PRODUCT_CODE    := CTR-P-HBRW
APP_UNIQUE_ID       := 0xFF3FF
APP_SYSTEM_MODE     := 64MB
APP_SYSTEM_MODE_EXT := 124MB
APP_SAVE_SIZE       := 0K

#---------------------------------------------------------------------------------
# code generation options (ARM11 / 3DS)
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

# --- embedded mGBA core (libmgba.a in external/mgba/build-3ds; see docs/kb/mgba-integration.md)
# MGBA_DEFS MUST match how libmgba.a was built or struct layouts drift (ABI corruption).
# (No FIXED_ROM_BUFFER: we rebuilt libmgba without it so each core owns its ROM.)
MGBA_DEFS := -DBUILD_STATIC -DCOLOR_16_BIT -DCOLOR_5_6_5 -DENABLE_DIRECTORIES \
             -DENABLE_SCRIPTING -DENABLE_VFS -DENABLE_VFS_FD -DM_CORE_GB -DM_CORE_GBA \
             -DUSE_LZMA -DUSE_MINIZIP -DUSE_PNG -DUSE_ZLIB
MGBA_INC := -I$(TOPDIR)/external/mgba/include -I$(TOPDIR)/external/mgba/build-3ds/include
MGBA_LIBDIR := $(TOPDIR)/external/mgba/build-3ds
# libmgba.a needs mGBA's bundled zlib + libpng (separate archives that cross-reference,
# so link them in one --start-group). lzma/minizip live inside libmgba.a.
MGBA_LDPATHS := -L$(MGBA_LIBDIR) -L$(MGBA_LIBDIR)/zlib -L$(MGBA_LIBDIR)/libpng
MGBA_LDLIBS  := -Wl,--start-group -lmgba -lzlibstatic -llibpng16_static -Wl,--end-group

CFLAGS	+=	$(MGBA_DEFS) $(MGBA_INC)

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= $(MGBA_LDLIBS) -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# library roots (each must contain include/ and lib/)
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# no need to edit below this line
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES 		:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) $(MGBA_LDPATHS)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

.PHONY: $(BUILD) clean all cia

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Build the installable .cia (depends on the .elf + .smdh produced by $(BUILD)).
cia: $(OUTPUT).cia

$(OUTPUT).cia: $(BUILD) banner.bnr
	@$(MAKEROM) -f cia -o $(OUTPUT).cia -target t -exefslogo \
		-elf $(OUTPUT).elf -rsf $(RSF) -icon $(OUTPUT).smdh -banner banner.bnr \
		-DAPP_TITLE="$(APP_TITLE)" \
		-DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" \
		-DAPP_UNIQUE_ID=$(APP_UNIQUE_ID) \
		-DAPP_SYSTEM_MODE=$(APP_SYSTEM_MODE) \
		-DAPP_SYSTEM_MODE_EXT=$(APP_SYSTEM_MODE_EXT) \
		-DAPP_SAVE_SIZE=$(APP_SAVE_SIZE)
	@echo built ... $(notdir $(OUTPUT).cia)

banner.bnr: $(BANNER_IMAGE) $(BANNER_AUDIO)
	@$(BANNERTOOL) makebanner -i $(BANNER_IMAGE) -a $(BANNER_AUDIO) -o banner.bnr

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia banner.bnr

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)
$(OFILES_SOURCES) : $(HFILES_BIN)
$(OUTPUT).elf	:	$(OFILES)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
