/* server_game.c - Multi-Room Price is Right Server */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include "common.h"

#define PORT 5500
#define MAX_CLIENTS 50
#define MAX_ROOMS 10
#define MAX_PLAYERS_PER_ROOM 5
#define IMAGE_CHUNK_SIZE 1024
#define ROUND_TIME_SEC 30

// --- STRUCTS ---
typedef struct {
    char username[64];
    char password[64];
    int wins;
    int games_played;
    int total_score;
} Account;

typedef struct {
    int socket_fd;
    char username[64];
    int is_logged_in;
    int room_id;         // 0 = Lobby
    
    // In-Game State
    int current_bid;
    struct timeval bid_time;
    int score;
} Player;

typedef struct {
    int id;
    int active;          // 0 = Free, 1 = Active
    int state;           // 0 = Waiting, 1 = Playing
    int player_count;
    int players[MAX_PLAYERS_PER_ROOM]; // Indices in global 'players' array
    
    int current_round;
    int max_rounds;
    int product_idx;     // Current product index
    struct timeval round_start_time;
    int owner_idx;       // Index of the owner in players array
} Room;

// --- GLOBALS ---
Account accounts[100];
int account_count = 0;

Product products[50];
int product_count = 0;

Player players[MAX_CLIENTS];
Room rooms[MAX_ROOMS];

// --- HELPERS ---
int read_n(int fd, void *buffer, int n) {
    int total_read = 0;
    char *buf = buffer;
    while (total_read < n) {
        int r = recv(fd, buf + total_read, n - total_read, 0);
        if (r <= 0) return r;
        total_read += r;
    }
    return total_read;
}

void send_packet(int fd, int type, const char *payload, int len) {
    uint32_t t = htonl(type);
    uint32_t l = htonl(len);
    if (send(fd, &t, 4, 0) < 0) return;
    if (send(fd, &l, 4, 0) < 0) return;
    if (len > 0) send(fd, payload, len, 0);
}

// Ensure persistent storage is updated
void save_accounts() {
    FILE *f = fopen("account.txt", "w");
    if (!f) return;
    for(int i=0; i<account_count; i++) {
        fprintf(f, "%s %s %d %d %d\n", accounts[i].username, accounts[i].password, accounts[i].wins, accounts[i].games_played, accounts[i].total_score);
    }
    fclose(f);
}

void load_accounts() {
    FILE *f = fopen("account.txt", "r");
    if (!f) return;
    account_count = 0;
    // format: user pass wins games score
    while (fscanf(f, "%s %s %d %d %d", accounts[account_count].username, accounts[account_count].password, 
                  &accounts[account_count].wins, &accounts[account_count].games_played, &accounts[account_count].total_score) == 5) {
        account_count++;
    }
    fclose(f);
    printf("Loaded %d accounts.\n", account_count);
}

// Just reusing the simple file format for now
void load_products() {
    FILE *f = fopen("products.txt", "r");
    if (!f) {
        strcpy(products[0].name, "Demo Item");
        products[0].price = 500;
        strcpy(products[0].image_file, "none.jpg");
        product_count = 1;
        return;
    }
    product_count = 0;
    while (fscanf(f, "%s %d %s", products[product_count].name, &products[product_count].price, products[product_count].image_file) == 3) {
        product_count++;
    }
    fclose(f);
    printf("Loaded %d products.\n", product_count);
}

// --- NETWORK BROADCAST ---
// Broadcast to all players in a specific room (or Lobby if room_id=0)
void broadcast_room(int room_id, int type, const char *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket_fd > 0 && players[i].is_logged_in && players[i].room_id == room_id) {
            send_packet(players[i].socket_fd, type, msg, strlen(msg));
        }
    }
}

// Send updated room list to everyone in Lobby
void broadcast_lobby_state() {
    char buf[4096] = ""; // "id:Players/Max:State;"
    
    for(int i=1; i<MAX_ROOMS; i++) { // Start 1, because 0 is Lobby
        if (rooms[i].active) {
            char item[128];
            snprintf(item, sizeof(item), "%d:%d/%d:%s;", 
                rooms[i].id, rooms[i].player_count, MAX_PLAYERS_PER_ROOM, 
                rooms[i].state == 0 ? "WAITING" : "PLAYING");
            strcat(buf, item);
        }
    }
    broadcast_room(0, PT_LOBBY_UPDATE, buf);
}

// Broadcast list of all online players to everyone (for invite UI)
void broadcast_player_list() {
    char buf[4096] = "";
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].socket_fd > 0 && players[i].is_logged_in) {
            char item[64];
            snprintf(item, sizeof(item), "%s:%d;", players[i].username, players[i].room_id);
            strcat(buf, item);
        }
    }
    // Send to all logged in
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].socket_fd > 0 && players[i].is_logged_in) {
            send_packet(players[i].socket_fd, PT_PLAYER_LIST, buf, strlen(buf));
        }
    }
}

// --- GAME LOGIC ---

void end_game(int r_idx) {
    // Determine winner
    int max_score = -1;
    int winner_idx = -1;
    char msg[1024] = "--- FINAL RESULTS ---\n";
    
    Room *room = &rooms[r_idx];
    
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].socket_fd > 0 && players[i].room_id == r_idx) {
            char line[64];
            snprintf(line, sizeof(line), "%s: %d pts\n", players[i].username, players[i].score);
            strcat(msg, line);
            
            // Update Stats
            for(int k=0; k<account_count; k++) {
                if(strcmp(accounts[k].username, players[i].username) == 0) {
                    accounts[k].games_played++;
                    accounts[k].total_score += players[i].score;
                }
            }

            if(players[i].score > max_score) {
                max_score = players[i].score;
                winner_idx = i;
            }
        }
    }
    
    if(winner_idx != -1) {
        for(int k=0; k<account_count; k++) {
            if(strcmp(accounts[k].username, players[winner_idx].username) == 0) {
                accounts[k].wins++;
            }
        }
        char w[100];
        snprintf(w, sizeof(w), "\nWINNER: %s!", players[winner_idx].username);
        strcat(msg, w);
    }
    
    save_accounts();
    broadcast_room(r_idx, PT_GAME_END, msg);
    
    // Reset Room
    room->state = 0; // Waiting
    room->current_round = 0;
    room->product_idx = 0;
    
    // Reset Player In-Game Stats
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].room_id == r_idx) {
            players[i].score = 0;
            players[i].current_bid = -1;
        }
    }
    
    broadcast_lobby_state(); // Status changed
}

void start_round(int r_idx) {
    Room *room = &rooms[r_idx];
    
    if(room->current_round >= room->max_rounds) {
        end_game(r_idx);
        return;
    }
    
    // Pick product (simple sequential for now)
    int p_idx = room->product_idx % product_count;
    room->product_idx++;
    room->current_round++;
    
    gettimeofday(&room->round_start_time, NULL);
    
    // Reset bids
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].room_id == r_idx) players[i].current_bid = -1;
    }
    
    // Send Info
    char msg[256];
    snprintf(msg, sizeof(msg), "Round %d/%d: %s", room->current_round, room->max_rounds, products[p_idx].name);
    broadcast_room(r_idx, PT_GAME_MSG, msg);
    
    // Send Question
    snprintf(msg, sizeof(msg), "%s", products[p_idx].name);
    broadcast_room(r_idx, PT_GAME_QUESTION, msg);

    // Update Stats HUD
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].socket_fd > 0 && players[i].room_id == r_idx) {
            char stats[64];
            snprintf(stats, sizeof(stats), "%d:%d:%d", room->current_round, room->max_rounds, players[i].score);
            send_packet(players[i].socket_fd, PT_GAME_STATS, stats, strlen(stats));
        }
    }

    // Send Image
    FILE *f = fopen(products[p_idx].image_file, "rb");
    if(f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char header[256];
        snprintf(header, sizeof(header), "%s:%ld", products[p_idx].image_file, sz);
        broadcast_room(r_idx, PT_IMAGE_START, header);
        
        char buf[IMAGE_CHUNK_SIZE];
        size_t n;
        while((n = fread(buf, 1, IMAGE_CHUNK_SIZE, f)) > 0) {
             for (int i = 0; i < MAX_CLIENTS; i++) {
                if (players[i].socket_fd > 0 && players[i].room_id == r_idx) {
                    send_packet(players[i].socket_fd, PT_IMAGE_DATA, buf, n);
                }
            }
        }
        fclose(f);
    }
}

void check_round_logic(int r_idx) {
    Room *room = &rooms[r_idx];
    // Check if everyone bid
    int all_bid = 1;
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].socket_fd > 0 && players[i].room_id == r_idx && players[i].current_bid == -1) {
            all_bid = 0;
            break;
        }
    }
    
    if(!all_bid) return; // Wait more
    
    // Calculate Scores
    int p_idx = (room->product_idx - 1) % product_count;
    int price = products[p_idx].price;
    
    int winner_idx = -1;
    double max_round_score = -1;
    
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].socket_fd > 0 && players[i].room_id == r_idx) {
            int bid = players[i].current_bid;
            if(bid <= price) {
                // Accuracy Score (0-1000)
                double accuracy = (double)bid / price;
                double base_score = accuracy * 1000.0;
                
                // Time Bonus
                double time_taken = (players[i].bid_time.tv_sec - room->round_start_time.tv_sec) + 
                                    (players[i].bid_time.tv_usec - room->round_start_time.tv_usec) / 1000000.0;
                double time_bonus = 0;
                if(time_taken < ROUND_TIME_SEC) {
                    time_bonus = (ROUND_TIME_SEC - time_taken) * 10; // 10 pts per sec saved
                }
                
                int total = (int)(base_score + time_bonus);
                players[i].score += total;
                
                // Send Private Breakdown
                char p_msg[128];
                snprintf(p_msg, sizeof(p_msg), "You earned %d pts (Accuracy: %d, Time Bonus: %d)", total, (int)base_score, (int)time_bonus);
                send_packet(players[i].socket_fd, PT_GAME_MSG, p_msg, strlen(p_msg));
                
                // Broadcast Public Detailed Score to OTHERS only
                char pub_msg[128];
                snprintf(pub_msg, sizeof(pub_msg), "%s earned %d pts (Accuracy: %d, Time Bonus: %d)", players[i].username, total, (int)base_score, (int)time_bonus);
                
                for(int j=0; j<MAX_CLIENTS; j++) {
                    if(players[j].socket_fd > 0 && players[j].room_id == r_idx && j != i) {
                        send_packet(players[j].socket_fd, PT_GAME_MSG, pub_msg, strlen(pub_msg));
                    }
                }
                
                if(total > max_round_score) {
                    max_round_score = total;
                    winner_idx = i;
                }
            } else {
                // Overbid message
                char p_msg[64];
                snprintf(p_msg, sizeof(p_msg), "You overbid! (Price: %d)", price);
                send_packet(players[i].socket_fd, PT_GAME_MSG, p_msg, strlen(p_msg));
            }
        }
    }
    
    char buf[512];
    if(winner_idx != -1) {
        snprintf(buf, sizeof(buf), "Price: %d. Winner: %s (+%d pts)!", price, players[winner_idx].username, (int)max_round_score);
    } else {
        snprintf(buf, sizeof(buf), "Price: %d. No winners (all overbid).", price);
    }
    broadcast_room(r_idx, PT_GAME_RESULT, buf);
    
    // Leaderboard
    char lb[1024] = "LEADERBOARD:\n";
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].room_id == r_idx) {
            char l[64];
            snprintf(l, sizeof(l), "%s: %d\n", players[i].username, players[i].score);
            strcat(lb, l);
        }
    }
    broadcast_room(r_idx, PT_GAME_LEADERBOARD, lb);
    
    // Update Stats HUD after scoring
    for(int i=0; i<MAX_CLIENTS; i++) {
        if(players[i].socket_fd > 0 && players[i].room_id == r_idx) {
            char stats[64];
            snprintf(stats, sizeof(stats), "%d:%d:%d", room->current_round, room->max_rounds, players[i].score);
            send_packet(players[i].socket_fd, PT_GAME_STATS, stats, strlen(stats));
        }
    }
    
    sleep(3);
    start_round(r_idx);
}

// --- PACKET HANDLING ---

void handle_client_msg(int idx) {
    int sock = players[idx].socket_fd;
    uint32_t type_net, len_net;
    
    if (read_n(sock, &type_net, 4) <= 0) {
        // Disconnect
        printf("Client %d disconnected.\n", idx);
        close(sock);
        // Clean up from room
        if(players[idx].room_id > 0) {
             Room *r = &rooms[players[idx].room_id];
             r->player_count--;
             if(r->player_count == 0) r->active = 0; 
             broadcast_lobby_state(); 
        }
        players[idx].socket_fd = 0;
        players[idx].is_logged_in = 0;
        players[idx].room_id = 0;
        return;
    }
    
    if (read_n(sock, &len_net, 4) <= 0) return;
    int type = ntohl(type_net);
    int len = ntohl(len_net);
    if(len > 1024 * 10 || len < 0) return; // Limit payload
    
    char *buf = malloc(len+1);
    if (len > 0) {
        if (read_n(sock, buf, len) <= 0) { 
            free(buf); 
            return; // Actual disconnect or error
        }
    }
    buf[len] = 0;
    
    // --- DISPATCH ---
    if (type == PT_LOGIN) {
        char *u = strtok(buf, ":");
        char *p = strtok(NULL, ":");
        // Simple auth
        int found = 0;
        for(int i=0; i<account_count; i++) {
            if(strcmp(accounts[i].username, u) == 0 && strcmp(accounts[i].password, p) == 0) {
                found = 1;
                strcpy(players[idx].username, u);
                players[idx].is_logged_in = 1;
                players[idx].room_id = 0; // Lobby
                send_packet(sock, PT_LOGIN_RESP, "OK", 2);
                broadcast_lobby_state(); // Send room list to newly logged in user
                broadcast_player_list(); /* NEW */
                break;
            }
        }
        if(!found) send_packet(sock, PT_LOGIN_RESP, "FAIL", 4);
    }
    else if (type == PT_REGISTER) {
        char *u = strtok(buf, ":");
        char *p = strtok(NULL, ":");
        // Check exists
        int exists = 0;
        for(int i=0; i<account_count; i++) if(strcmp(accounts[i].username, u) == 0) exists = 1;
        
        if(exists) {
            send_packet(sock, PT_LOGIN_RESP, "EXISTS", 6); // Reusing LOGIN_RESP for simple feedback
        } else {
            strcpy(accounts[account_count].username, u);
            strcpy(accounts[account_count].password, p);
            accounts[account_count].wins = 0;
            accounts[account_count].games_played = 0;
            accounts[account_count].total_score = 0;
            account_count++;
            save_accounts();
            send_packet(sock, PT_LOGIN_RESP, "REG_OK", 6);
        }
    }
    else if (type == PT_CREATE_ROOM) {
        // Find empty room
        int r_id = -1;
        for(int i=1; i<MAX_ROOMS; i++) {
            if(!rooms[i].active) {
                r_id = i;
                break;
            }
        }
        if (r_id != -1) {
            rooms[r_id].active = 1;
            rooms[r_id].id = r_id; // Explicit
            rooms[r_id].player_count = 1;
            rooms[r_id].current_round = 0;
            rooms[r_id].max_rounds = 5;
            rooms[r_id].state = 0; // Waiting
            
            rooms[r_id].owner_idx = idx; // Assign Owner
            
            players[idx].room_id = r_id;
            
            broadcast_lobby_state(); // Notify others
            broadcast_player_list(); /* NEW: Update player list status for creator */
            char msg[64]; snprintf(msg, sizeof(msg), "Room %d Created (You are Owner)", r_id);
            send_packet(sock, PT_GAME_MSG, msg, strlen(msg));
        }
    }
    else if (type == PT_JOIN_ROOM) {
         int target_id = atoi(buf);
         if(target_id > 0 && target_id < MAX_ROOMS && rooms[target_id].active) {
             if (rooms[target_id].state == 0 && rooms[target_id].player_count < MAX_PLAYERS_PER_ROOM) {
                players[idx].room_id = target_id;
                rooms[target_id].player_count++;
                
                broadcast_lobby_state();
                broadcast_player_list(); /* NEW */
                
                char msg[64]; snprintf(msg, sizeof(msg), "%s joined Room %d", players[idx].username, target_id);
                broadcast_room(target_id, PT_GAME_MSG, msg);
            } else {        }
        }
    }
    else if (type == PT_START_GAME) {
        int r_id = players[idx].room_id;
        if(r_id > 0 && rooms[r_id].active && rooms[r_id].owner_idx == idx) {
            if(rooms[r_id].state == 0) {
                rooms[r_id].state = 1; // Playing
                broadcast_room(r_id, PT_GAME_MSG, "Owner started the game!");
                start_round(r_id);
                broadcast_lobby_state();
            }
        }
    }
    else if (type == PT_LEAVE_ROOM) {
        int r = players[idx].room_id;
        if(r > 0) {
            rooms[r].player_count--;
            players[idx].room_id = 0;
            players[idx].score = 0;
            if(rooms[r].player_count == 0) rooms[r].active = 0;
            broadcast_lobby_state();
            broadcast_player_list(); /* NEW */
            send_packet(sock, PT_GAME_MSG, "Left Room", 9);
        }
    }
    else if (type == PT_GAME_BID) {
        int bid = atoi(buf);
        if(players[idx].room_id > 0) {
            players[idx].current_bid = bid;
            gettimeofday(&players[idx].bid_time, NULL);
            
            // Broadcast Bid
            char msg[64];
            snprintf(msg, sizeof(msg), "%s bid: %d", players[idx].username, bid);
            broadcast_room(players[idx].room_id, PT_GAME_MSG, msg);
            
            check_round_logic(players[idx].room_id);
        }
    }
    else if (type == PT_INVITE) {
        // buf = "TargetUsername"
        int target_idx = -1;
        for(int i=0; i<MAX_CLIENTS; i++) {
            if(strcmp(players[i].username, buf) == 0 && players[i].socket_fd > 0) {
                target_idx = i;
                break;
            }
        }
        
        if (target_idx != -1) {
            if (players[target_idx].room_id == 0) {
                // Forward Invite: "RoomID:SenderName"
                char invite_msg[128];
                snprintf(invite_msg, sizeof(invite_msg), "%d:%s", players[idx].room_id, players[idx].username);
                send_packet(players[target_idx].socket_fd, PT_INVITE, invite_msg, strlen(invite_msg));
                send_packet(sock, PT_GAME_MSG, "Invite sent.", 12);
            } else {
                send_packet(sock, PT_GAME_MSG, "User is busy (in a game).", 25);
            }
        } else {
            send_packet(sock, PT_GAME_MSG, "User not found.", 15);
        }
    }
    else if (type == PT_INVITE_RESP) {
        // buf = "SenderName:DECISION"
        char *sender_name = strtok(buf, ":");
        char *decision = strtok(NULL, ":");
        
        int sender_idx = -1;
        for(int i=0; i<MAX_CLIENTS; i++) {
            if(strcmp(players[i].username, sender_name) == 0) {
                sender_idx = i;
                break;
            }
        }
        
        if (sender_idx != -1 && decision) {
            if (strcmp(decision, "ACCEPT") == 0) {
                int r_id = players[sender_idx].room_id;
                // Move player (Logic copied from JOIN_ROOM)
                if(r_id > 0 && rooms[r_id].active && rooms[r_id].player_count < MAX_PLAYERS_PER_ROOM && rooms[r_id].state == 0) {
                    players[idx].room_id = r_id;
                    rooms[r_id].player_count++;
                    
                    broadcast_lobby_state();
                    broadcast_player_list(); // NEW: Update player list on invite accept
                    
                    char msg[64]; snprintf(msg, sizeof(msg), "%s joined via invite", players[idx].username);
                    broadcast_room(r_id, PT_GAME_MSG, msg);
                } else {
                   send_packet(sock, PT_GAME_MSG, "Room full or started.", 21);
                }
            } else {
                send_packet(players[sender_idx].socket_fd, PT_GAME_MSG, "User declined invite.", 21);
            }
        }
    }

    free(buf);
}

int main() {
    load_accounts();
    load_products();
    
    int master = socket(AF_INET, SOCK_STREAM, 0);
    int opt=1;
    setsockopt(master, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(master, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    if (listen(master, 10) < 0) {
        perror("Listen failed");
        exit(1);
    }
    
    printf("Server Multi-Room running on %d\n", PORT);
    
    // Init Globals
    for(int i=0; i<MAX_CLIENTS; i++) players[i].socket_fd = 0;
    for(int i=0; i<MAX_ROOMS; i++) rooms[i].active = 0;
    
    fd_set readfds;
    while(1) {
        FD_ZERO(&readfds);
        FD_SET(master, &readfds);
        int max_sd = master;
        
        for(int i=0; i<MAX_CLIENTS; i++) {
            if(players[i].socket_fd > 0) {
                FD_SET(players[i].socket_fd, &readfds);
                if(players[i].socket_fd > max_sd) max_sd = players[i].socket_fd;
            }
        }
        
        select(max_sd+1, &readfds, NULL, NULL, NULL);
        
        if(FD_ISSET(master, &readfds)) {
            int new_sock = accept(master, NULL, NULL);
            for(int i=0; i<MAX_CLIENTS; i++) {
                if(players[i].socket_fd == 0) {
                    players[i].socket_fd = new_sock;
                    players[i].is_logged_in = 0;
                    players[i].room_id = 0;
                    break;
                }
            }
        }
        
        for(int i=0; i<MAX_CLIENTS; i++) {
            if(players[i].socket_fd > 0 && FD_ISSET(players[i].socket_fd, &readfds)) {
                handle_client_msg(i);
            }
        }
    }
    return 0;
}
