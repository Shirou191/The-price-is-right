#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

// Packet Types
enum {
    PT_LOGIN = 1,
    PT_LOGIN_RESP = 2,
    PT_REGISTER = 3,         // NEW: Register
    PT_GAME_MSG = 4,
    PT_GAME_QUESTION = 5,
    PT_GAME_BID = 6,
    PT_GAME_RESULT = 7,
    PT_GAME_LEADERBOARD = 8,
    PT_IMAGE_START = 9,
    PT_IMAGE_DATA = 10,
    PT_GAME_END = 11,
    
    // Room / Lobby
    PT_LOBBY_UPDATE = 12,    // Server sends room list
    PT_CREATE_ROOM = 13,     // Client creates room
    PT_JOIN_ROOM = 14,       // Client joins room
    PT_LEAVE_ROOM = 15,      // Client leaves room
    PT_INVITE = 16,          // Invite player
    PT_INVITE_RESP = 17,      // Respond to invite
    PT_GAME_STATS = 18,       // Update Score/Round
    PT_START_GAME = 19,       // Owner starts game
    PT_PLAYER_LIST = 20       // List of online players
};

// Product Structure
typedef struct {
    char name[100];
    int price;
    char image_file[100];
} Product;

#endif
