# Builds ClawXe2.kext on macOS (x86_64). Requires Xcode CLT, python3 and MacKernelSDK:
#   git clone https://github.com/acidanthera/MacKernelSDK
#   tools/fetch-firmware.sh          # GuC firmware for milestone 3 (optional)
#   make SDK=./MacKernelSDK
SDK     ?= MacKernelSDK
FW_DIR  ?= firmware
NAME    := ClawXe2
SRC     := kext/ClawXe2
OUT     := build/$(NAME).kext
ARCH    := -arch x86_64 -mmacosx-version-min=11.0
KFLAGS  := $(ARCH) -mkernel -nostdlib -fno-builtin -fno-common -fno-stack-protector \
           -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
           -I$(SDK)/Headers -Wall -Wextra -Wno-unused-parameter -O2
CXXFLAGS := $(KFLAGS) -std=c++17 -fno-rtti -fno-exceptions -fapple-kext
CFLAGS   := $(KFLAGS) -std=c11

OBJS := build/ClawXe2.o build/Xe2Mem.o build/Xe2Guc.o build/kmod_info.o build/guc_fw.o

all: $(OUT)

build/%.o: $(SRC)/%.cpp $(SRC)/*.h $(SRC)/*.hpp
	@mkdir -p build
	clang++ $(CXXFLAGS) -c $< -o $@

build/%.o: $(SRC)/%.c
	@mkdir -p build
	clang $(CFLAGS) -c $< -o $@

build/guc_fw.c: $(wildcard $(FW_DIR)/lnl_guc_70.bin) tools/embed-firmware.py
	@mkdir -p build
	python3 tools/embed-firmware.py $(FW_DIR)/lnl_guc_70.bin $@ xe/lnl_guc_70.bin --allow-missing

build/guc_fw.o: build/guc_fw.c
	clang $(CFLAGS) -c $< -o $@

$(OUT): $(OBJS) $(SRC)/Info.plist
	@mkdir -p $(OUT)/Contents/MacOS $(OUT)/Contents/Resources
	clang++ $(ARCH) -mkernel -nostdlib -Xlinker -kext -L$(SDK)/Library/x86_64 \
	    -lkmod -lkmodc++ -lcc_kext $(OBJS) -o $(OUT)/Contents/MacOS/$(NAME)
	cp $(SRC)/Info.plist $(OUT)/Contents/Info.plist
	if [ -f $(FW_DIR)/LICENSE.xe ]; then cp $(FW_DIR)/LICENSE.xe $(OUT)/Contents/Resources/; fi
	plutil -lint $(OUT)/Contents/Info.plist

clean:
	rm -rf build

.PHONY: all clean
