# Automatically generated by configure - do not modify
CONFIG_QEMU_PREFIX="/usr/gnemul/qemu-mips64"
TARGET_ABI_MIPSN64=y
TARGET_ARCH=mips64
TARGET_MIPS64=y
TARGET_ARCH2=mips64
TARGET_BASE_ARCH=mips
TARGET_ABI_DIR=mips64
TARGET_PHYS_ADDR_BITS=64
TARGET_WORDS_BIGENDIAN=y
CONFIG_SOFTMMU=y
LIBS+=-lutil -lncurses  -ljpeg -L/usr/lib/x86_64-linux-gnu -lSDL -lX11 
HWLIB=../libhw64/libqemuhw64.a
CONFIG_SOFTFLOAT=y
CONFIG_I386_DIS=y
CONFIG_MIPS_DIS=y
LDFLAGS+=
QEMU_CFLAGS+=-DHAS_AUDIO -DHAS_AUDIO_CHOICE -I$(SRC_PATH)/fpu -I$(SRC_PATH)/tcg -I$(SRC_PATH)/tcg/$(ARCH) 
