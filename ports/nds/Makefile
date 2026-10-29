# Independent N02 diagnostics. Invoke from ports/nds in the SDK shell.
# SDK flags/specs follow the official BlocksDS ARM9-only build interface.
BLOCKSDS ?= /opt/blocksds/core
WONDERFUL_TOOLCHAIN ?= /opt/wonderful
ARM_NONE_EABI_PATH ?= $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/
ARM7ELF ?= $(BLOCKSDS)/sys/arm7/main_core/arm7_maxmod.elf

OUTDIR := ../../build/nds/n02
OBJDIR := $(OUTDIR)/obj
NAME := dok_nds_diag
ELF := $(OUTDIR)/$(NAME).elf
MAP := $(OUTDIR)/$(NAME).map
ROM := $(OUTDIR)/$(NAME).nds
OBJECT := $(OBJDIR)/main.o

CC := $(ARM_NONE_EABI_PATH)arm-none-eabi-gcc
NDSTOOL := $(BLOCKSDS)/tools/ndstool/ndstool
ARCH := -mthumb -mcpu=arm946e-s+nofp
SPECS := $(BLOCKSDS)/sys/crts/ds_arm9.specs
CPPFLAGS := -D__NDS__ -D__BLOCKSDS__ -DARM9 -I$(BLOCKSDS)/libs/libnds/include
# libnds headers use GNU inline asm; this diagnostic target needs GNU C11.
# The independent game core will choose its own strict-C11 host flags in N03.
CFLAGS := -std=gnu11 -Og -g3 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
LDFLAGS := -L$(BLOCKSDS)/libs/libnds/lib -Wl,-Map,$(MAP)

.PHONY: all
all: $(ROM)

$(ROM): $(ELF) $(ARM7ELF)
	"$(NDSTOOL)" -c "$@" -9 "$(ELF)" -7 "$(ARM7ELF)"

$(ELF): $(OBJECT)
	"$(CC)" $(ARCH) -specs=$(SPECS) $(CPPFLAGS) -o "$@" $(OBJECT) $(LDFLAGS) -Wl,--start-group -lnds9 -lc -Wl,--end-group

$(OBJECT): platform/diag/main.c platform/diag/diagnostic_state.h Makefile | $(OBJDIR)
	"$(CC)" $(ARCH) -specs=$(SPECS) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(OBJDIR):
	mkdir -p "$@"

-include $(OBJECT:.o=.d)
