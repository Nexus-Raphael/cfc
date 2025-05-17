CC = gcc
CFLAGS = -ID:/Develop/VScode/curl/include -ID:/Develop/VScode/cJSON
LDFLAGS = -LD:/Develop/VScode/curl/lib -LD:/Develop/VScode/cJSON
LIBS = -lcurl -lcjson -lws2_32 -lssl -lcrypto -lz
TARGET = jspra.exe	##

SRC = src/prjs.c		##
 
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

copy_dll:
	copy "D:/Develop/VScode/curl/lib/libcurl.dll" .

clean:
	del $(TARGET) libcurl.dll