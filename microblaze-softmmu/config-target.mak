# Automatically generated by configure - do not modify
CONFIG_QEMU_PREFIX="/usr/gnemul/qemu-microblaze"
TARGET_ARCH=microblaze
TARGET_MICROBLAZE=y
TARGET_ARCH2=microblaze
TARGET_BASE_ARCH=microblaze
TARGET_ABI_DIR=microblaze
TARGET_PHYS_ADDR_BITS=64
TARGET_WORDS_BIGENDIAN=y
CONFIG_SOFTMMU=y
LIBS+=-lutil -lncurses  -ljpeg -L/usr/lib/x86_64-linux-gnu -lSDL -lX11 
HWLIB=../libhw64/libqemuhw64.a
CONFIG_SOFTFLOAT=y
CONFIG_I386_DIS=y
CONFIG_MICROBLAZE_DIS=y
CONFIG_NEED_MMU=y
LDFLAGS+=
QEMU_CFLAGS+=-I$(SRC_PATH)/fpu -I$(SRC_PATH)/tcg -I$(SRC_PATH)/tcg/$(ARCH) 