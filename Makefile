SRC_DIR    := src
BUILD_DIR  := build
BIN_DIR    := .
CURL_DLL := .\libs\curl\bin\libcurl-x64.dll




APP_SRCS     := $(SRC_DIR)/api.c $(SRC_DIR)/main.c
APP_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
APP_TARGET   := $(BIN_DIR)/cfclawer.exe


CC        := gcc
CFLAGS    := -Ilibs/curl/include \
             -Ilibs/cjson/include
LDFLAGS   := -Llibs/curl/lib \
             -Llibs/cjson/lib

LIBS      := -lcurl -lcjson -lws2_32 -lssl -lcrypto -lz


.PHONY: all
all: prepare $(APP_TARGET) copy_dll copy_cert

.PHONY: prepare
prepare:
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(APP_TARGET): $(APP_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS) $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $< -o $@ $(CFLAGS)


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

.PHONY: clean
clean:
	del /Q "$(BUILD_DIR)\api.o" "$(BUILD_DIR)\main.o"
	del /Q "$(APP_TARGET)"
