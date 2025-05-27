SRC_DIR    := src
BUILD_DIR  := build
BIN_DIR    := .
CURL_DLL := D:\Develop\VScode\curl\bin\libcurl-x64.dll

# -------------------------------------------------------------------
# 源文件、目标文件、可执行名
# -------------------------------------------------------------------

APP_SRCS     := $(SRC_DIR)/api.c $(SRC_DIR)/main.c
APP_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
APP_TARGET   := $(BIN_DIR)/cf_clawer.exe

# 下面两项只是示例“测试文件”，默认不参与 all 或 clean
TEST_SRC     := $(SRC_DIR)/prjs.c
TEST_TARGET  := $(BIN_DIR)/jspra.exe

# -------------------------------------------------------------------
# 编译器和选项
# -------------------------------------------------------------------
CC        := gcc
CFLAGS    := -ID:/Develop/VScode/curl/include \
              -ID:/Develop/VScode/cJSON
LDFLAGS   := -LD:/Develop/VScode/curl/lib \
              -LD:/Develop/VScode/cJSON
LIBS      := -lcurl -lcjson -lws2_32 -lssl -lcrypto -lz

# -------------------------------------------------------------------
# 使用mingw32-make test即可编译测试文件
# -------------------------------------------------------------------
.PHONY: test
test: prepare $(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRC)
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

# -------------------------------------------------------------------
# 默认目标：编译主程序 + 拷 DLL & 证书
# -------------------------------------------------------------------
.PHONY: all
all: prepare $(APP_TARGET) copy_dll copy_cert

# 确保 build 目录存在
.PHONY: prepare
prepare:
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

# -------------------------------------------------------------------
# 链接主可执行文件
# -------------------------------------------------------------------
$(APP_TARGET): $(APP_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS) $(LIBS)

# -------------------------------------------------------------------
# 编译规则：src/xxx.c → build/xxx.o
# -------------------------------------------------------------------
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# -------------------------------------------------------------------
# 运行时依赖拷贝
# -------------------------------------------------------------------
.PHONY: copy_dll
copy_dll:
	@if exist "$(CURL_DLL)" ( \
	  echo Copying libcurl.dll from $(CURL_DLL) && \
	  copy "$(CURL_DLL)" "$(BIN_DIR)\libcurl.dll" \
	) else ( \
	  echo Error: libcurl.dll not found at $(CURL_DLL) && exit 1 \
	)

.PHONY: copy_cert
copy_cert:
	@rem 确保 certs 目录存在
	@if not exist "$(BIN_DIR)\certs" mkdir "$(BIN_DIR)\certs"
	@rem 只有当目标文件不存在时才拷贝
	@if not exist "$(BIN_DIR)\certs\cacert.pem" ( \
	  echo Copying cacert.pem && \
	  copy "certs\cacert.pem" "$(BIN_DIR)\certs\" \
	) else ( \
	  echo cacert.pem already exists, skipping copy. \
	)
# -------------------------------------------------------------------
# 清理：只删主程序的中间文件和可执行文件，不触碰 prjs.c/jspra.exe
# -------------------------------------------------------------------
.PHONY: clean
clean:
	del /Q "$(BUILD_DIR)\api.o" "$(BUILD_DIR)\main.o"
	del /Q "$(APP_TARGET)"

# -------------------------------------------------------------------
# 在clean的基础上同时删除测试文件jspra.exe
# -------------------------------------------------------------------
.PHONY: clean_all 
clean_all: clean
	del /Q "$(TEST_TARGET)"

# -------------------------------------------------------------------
# 单独删除jspra.exe
# -------------------------------------------------------------------
.PHONY: clean_test
clean_test:
	del /Q "$(TEST_TARGET)"