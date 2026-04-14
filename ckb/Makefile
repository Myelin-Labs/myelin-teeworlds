CLANG := clang
CLANGXX := $(subst clang,clang++,$(CLANG))
LD := $(subst clang,ld.lld,$(CLANG))
OBJCOPY := $(subst clang,llvm-objcopy,$(CLANG))

MUSL := $(realpath musl)
MUSL_TARGET := $(MUSL)/release/lib/libc.a
BUILTINS := $(realpath builtins)
BUILTINS_TARGET := $(BUILTINS)/build/libcompiler-rt.a
LIBCXX := $(realpath libcxx)
LIBCXX_TARGET := $(LIBCXX)/release/lib/libc++.a

TEEWORLDS := $(realpath ..)
CKB_STDLIB := $(realpath ckb-c-stdlib)

DEBUG := false
REORDER_STACK := true

BASE_CFLAGS := --target=riscv64 -march=rv64imc_zba_zbb_zbc_zbs \
	-Os -DCKB \
	-fdata-sections -ffunction-sections -fvisibility=hidden \
	-I $(TEEWORLDS)/src -I $(TEEWORLDS)/build/src \
	-I $(TEEWORLDS)/src/engine/external/zlib \
	-I $(CKB_STDLIB) -I $(CKB_STDLIB)/molecule

LDFLAGS := --gc-sections --static \
  --nostdlib --sysroot $(MUSL)/release \
  -L$(MUSL)/release/lib -L$(BUILTINS)/build \
  -lc -lgcc -lcompiler-rt \
  -L$(LIBCXX)/release/lib \
  -lc++ -lc++abi -lunwind

ifeq (true,$(REORDER_STACK))
	BASE_CFLAGS += -DCKB_REORDER_STACK
	LDFLAGS += -Tld_interface.ld
endif

CFLAGS := -g \
  -Wall -Werror \
  -Wno-unused-function \
  -nostdinc \
  -isystem $(MUSL)/release/include \
  $(BASE_CFLAGS)

CXXFLAGS := -g \
  -Wall -Werror \
  -std=c++20 \
  -D_GNU_SOURCE \
  -nostdinc -nostdinc++ \
  -isystem $(LIBCXX)/release/include/c++/v1 \
  -isystem $(MUSL)/release/include \
  $(BASE_CFLAGS)
ifneq (true,$(DEBUG))
	CXXFLAGS += -DNO_DEBUG_INFO
endif

all: build/replayer build/replayer_stripped

TEEWORLDS_LIBS = main.o system.o bootloader.o
# src/base
TEEWORLDS_LIBS += hash_libtomcrypt.o hash.o
# src/engine/shared
TEEWORLDS_LIBS += packer.o compression.o kernel.o memheap.o storage.o \
	console.o linereader.o map.o datafile.o config.o
# src/game/server
TEEWORLDS_LIBS += gamecontext.o player.o gamecontroller.o gameworld.o \
	entity.o eventhandler.o
# src/game/server/entities
TEEWORLDS_LIBS += character.o laser.o pickup.o projectile.o flag.o
# src/game/server/gamemodes
TEEWORLDS_LIBS += mod.o ctf.o lms.o dm.o lts.o tdm.o
# src/game
TEEWORLDS_LIBS += layers.o collision.o gamecore.o
# build/src/generated
TEEWORLDS_LIBS += protocol.o server_data.o
# src/engine/external/zlib
TEEWORLDS_LIBS += zlib_adler32.o zlib_compress.o zlib_crc32.o \
	zlib_deflate.o zlib_infback.o zlib_inffast.o zlib_inflate.o \
	zlib_inftrees.o zlib_trees.o zlib_uncompr.o zlib_zutil.o

build/replayer_stripped: build/replayer
	$(OBJCOPY) --strip-all $< $@

build/replayer: $(foreach o,$(TEEWORLDS_LIBS),build/$(o)) $(BUILTINS_TARGET)
	$(LD) $(foreach o,$(TEEWORLDS_LIBS),build/$(o)) -o $@ $(LDFLAGS)

build/system.o: system.c $(MUSL_TARGET)
	$(CLANG) -c $< -o $@ $(CFLAGS)

build/%.o: $(TEEWORLDS)/src/base/%.c $(MUSL_TARGET)
	$(CLANG) -c $< -o $@ $(CFLAGS)

build/%.o: $(TEEWORLDS)/src/engine/shared/%.cpp $(MUSL_TARGET) $(LIBCXX_TARGET)
	$(CLANGXX) -c $< -o $@ $(CXXFLAGS)

build/%.o: $(TEEWORLDS)/src/game/server/%.cpp $(MUSL_TARGET) $(LIBCXX_TARGET)
	$(CLANGXX) -c $< -o $@ $(CXXFLAGS)

build/%.o: $(TEEWORLDS)/src/game/server/entities/%.cpp $(MUSL_TARGET) $(LIBCXX_TARGET)
	$(CLANGXX) -c $< -o $@ $(CXXFLAGS)

build/%.o: $(TEEWORLDS)/src/game/server/gamemodes/%.cpp $(MUSL_TARGET) $(LIBCXX_TARGET)
	$(CLANGXX) -c $< -o $@ $(CXXFLAGS)

build/%.o: $(TEEWORLDS)/src/game/%.cpp $(MUSL_TARGET) $(LIBCXX_TARGET)
	$(CLANGXX) -c $< -o $@ $(CXXFLAGS)

build/%.o: $(TEEWORLDS)/build/src/generated/%.cpp $(MUSL_TARGET) $(LIBCXX_TARGET)
	$(CLANGXX) -c $< -o $@ $(CXXFLAGS)

build/zlib_%.o: $(TEEWORLDS)/src/engine/external/zlib/%.c $(MUSL_TARGET)
	$(CLANG) -c $< -o $@ $(CFLAGS) -std=gnu99 -Wno-deprecated-non-prototype

build/%.o: %.cpp $(MUSL_TARGET) $(LIBCXX_TARGET)
	$(CLANGXX) -c $< -o $@ $(CXXFLAGS)

build/%.o: %.S $(MUSL_TARGET)
	$(CLANG) -c $< -o $@ $(CFLAGS)

MUSL_CFLAGS := $(BASE_CFLAGS) -DPAGE_SIZE=4096
# ifneq (true,$(DEBUG))
# 	MUSL_CFLAGS += -DCKB_MUSL_DUMMIFY_PRINTF
# endif

$(MUSL_TARGET):
	cd $(MUSL) && \
		CLANG=$(CLANG) \
			BASE_CFLAGS="$(MUSL_CFLAGS)" \
			./ckb/build.sh

BUILTINS_CFLAGS := --target=riscv64  -march=rv64imc_zba_zbb_zbc_zbs -mabi=lp64 
BUILTINS_CFLAGS += -nostdinc -I $(MUSL)/release/include -I $(LIBCXX)/release/include
BUILTINS_CFLAGS += -Os -g
BUILTINS_CFLAGS += -fdata-sections -ffunction-sections -fno-builtin -fvisibility=hidden -fomit-frame-pointer
BUILTINS_CFLAGS += -I compiler-rt/lib/builtins
BUILTINS_CFLAGS += -DVISIBILITY_HIDDEN -DCOMPILER_RT_HAS_FLOAT16

$(BUILTINS_TARGET): $(MUSL_TARGET) $(LIBCXX_TARGET)
	cd $(BUILTINS) && \
		make CC=$(CLANG) \
			AR=$(subst clang,llvm-ar,$(CLANG)) \
			CFLAGS="$(BUILTINS_CFLAGS)"

LLVM_CMAKE_OPTIONS := -DCMAKE_BUILD_TYPE=MinSizeRel
# LLVM_CMAKE_OPTIONS += -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF -DLIBCXX_ENABLE_UNICODE=OFF
LLVM_CMAKE_OPTIONS += -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF
LLVM_CMAKE_OPTIONS += -DLIBCXXABI_SILENT_TERMINATE=ON

$(LIBCXX_TARGET): $(MUSL_TARGET)
	cd $(LIBCXX) && \
		CLANG=$(CLANG) \
			BASE_CFLAGS="$(BASE_CFLAGS)" \
			MUSL=$(MUSL)/release \
			DEBUG=true \
			LLVM_VERSION="18.1.8" \
			LLVM_PATCH="$(realpath llvm_patch)" \
			LLVM_CMAKE_OPTIONS="$(LLVM_CMAKE_OPTIONS)" \
		  ./build.sh
	touch $@

clean:
	rm -rf build/replayer* build/*.o
	cd $(MUSL) && make clean && rm -rf release
	cd $(BUILTINS) && make clean
	cd $(LIBCXX) && rm -rf release build llvm_src

force-rebuild:
	rm -rf build/replayer* build/*.o
	$(MAKE)

.PHONY: clean all force-rebuild
