# Makefile cho Project Game (GTK Client + Server)

CC = gcc
CFLAGS = -Wall -O2
# Cờ thư viện cho GTK
GTK_FLAGS = `pkg-config --cflags --libs gtk+-3.0`

SERVER_BIN = server_game
CLIENT_BIN = client_game
GUI_BIN = client_gui

all: $(SERVER_BIN) $(CLIENT_BIN) $(GUI_BIN)

# Biên dịch Server (Không đổi)
$(SERVER_BIN): server_game.c common.h
	$(CC) $(CFLAGS) -o $(SERVER_BIN) server_game.c

# Biên dịch Client Console Cũ (Không đổi)
$(CLIENT_BIN): client_game.c common.h
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) client_game.c

# Biên dịch Client GUI Mới
$(GUI_BIN): client_gui.c common.h
	$(CC) $(CFLAGS) -o $(GUI_BIN) client_gui.c $(GTK_FLAGS)

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(GUI_BIN)
	rm -f gui_recv_*.jpg recv_*.jpg

.PHONY: all clean
