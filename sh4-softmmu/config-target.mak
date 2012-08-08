# Automatically generated by configure - do not modify
CONFIG_QEMU_PREFIX="/usr/gnemul/qemu-sh4"
TARGET_ARCH=sh4
TARGET_SH4=y
TARGET_ARCH2=sh4
TARGET_BASE_ARCH=sh4
TARGET_ABI_DIR=sh4
TARGET_PHYS_ADDR_BITS=64
CONFIG_SOFTMMU=y
LIBS+=-lutil -lncurses  -ljpeg -L/usr/lib/x86_64-linux-gnu -lSDL -lX11 
HWLIB=../libhw64/libqemuhw64.a
CONFIG_NOSOFTFLOAT=y
CONFIG_I386_DIS=y
CONFIG_SH4_DIS=y
LDFLAGS+=
QEMU_CFLAGS+=-I$(SRC_PATH)/fpu -I$(SRC_PATH)/tcg -I$(SRC_PATH)/tcg/$(ARCH) 
