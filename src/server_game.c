/* server_game.c - The Price is Right Server (Complete Fixed) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <errno.h>
#include "common.h"

#define PORT 5500
#define MAX_CLIENTS 10
#define MIN_PLAYERS_TO_START 2
#define IMAGE_CHUNK_SIZE 1024

typedef struct {
    char username[64];
    char password[64];
    int status; // 1=Active
} Account;

typedef struct {
    int socket_fd;
    char username[64];
    int is_logged_in;
    int current_bid; 
    int score; // Điểm số tích lũy
} Player;

// Globals
Account accounts[100];
int account_count = 0;

Product products[50];
int product_count = 0;

Player players[MAX_CLIENTS];
int current_product_idx = 0;
int game_started = 0;

// --- HÀM HỖ TRỢ ĐỌC/GHI MẠNG ---

// Hàm đọc N bytes đảm bảo (Fix lỗi packet fragmentation)
int read_n(int fd, void *buffer, int n) {
    int total_read = 0;
    char *buf = buffer;
    while (total_read < n) {
        int r = recv(fd, buf + total_read, n - total_read, 0);
        if (r <= 0) return r; // Lỗi hoặc đóng kết nối
        total_read += r;
    }
    return total_read;
}

void send_packet(int fd, int type, void *payload, int len) {
    uint32_t t = htonl(type);
    uint32_t l = htonl(len);
    if (send(fd, &t, 4, 0) < 0) return;
    if (send(fd, &l, 4, 0) < 0) return;
    if (len > 0) send(fd, payload, len, 0);
}

void broadcast(int type, char *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket_fd > 0 && players[i].is_logged_in) {
            send_packet(players[i].socket_fd, type, msg, strlen(msg));
        }
    }
}

// --- TẢI DỮ LIỆU ---
void load_accounts() {
    FILE *f = fopen("account.txt", "r");
    if (!f) return;
    account_count = 0;
    int temp_wrong;
    while (fscanf(f, "%s %s %d %d", accounts[account_count].username, accounts[account_count].password, &accounts[account_count].status, &temp_wrong) == 4) {
        account_count++;
    }
    fclose(f);
    printf("Loaded %d accounts.\n", account_count);
}

void load_products() {
    FILE *f = fopen("products.txt", "r");
    if (!f) {
        printf("Lỗi: Không tìm thấy products.txt. Tạo dữ liệu mẫu...\n");
        strcpy(products[0].name, "San_Pham_Mau");
        products[0].price = 1000;
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

int check_login(char *user, char *pass) {
    for (int i = 0; i < account_count; i++) {
        if (strcmp(accounts[i].username, user) == 0 && strcmp(accounts[i].password, pass) == 0) {
            if (accounts[i].status == 0) return -1; // Blocked
            return 1; // OK
        }
    }
    return 0; // Fail
}

// --- GỬI ẢNH ---
void broadcast_image(char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        char err[100];
        snprintf(err, sizeof(err), "Lỗi: Không tìm thấy ảnh %s", filename);
        broadcast(PT_GAME_MSG, err);
        return;
    }

    fseek(f, 0, SEEK_END);
    long filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Header: "filename:size"
    char img_header[256];
    snprintf(img_header, sizeof(img_header), "%s:%ld", filename, filesize);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket_fd > 0 && players[i].is_logged_in) {
            send_packet(players[i].socket_fd, PT_IMAGE_START, img_header, strlen(img_header));
        }
    }

    // Data chunks
    char buffer[IMAGE_CHUNK_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, IMAGE_CHUNK_SIZE, f)) > 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (players[i].socket_fd > 0 && players[i].is_logged_in) {
                send_packet(players[i].socket_fd, PT_IMAGE_DATA, buffer, bytes_read);
            }
        }
    }
    fclose(f);
    printf("Đã gửi ảnh %s (%ld bytes) tới các client.\n", filename, filesize);
}

// --- GAME LOGIC ---
void announce_winner() {
    int max_score = -1;
    int winner_idx = -1;
    char msg[1024] = "--- KẾT QUẢ CHUNG CUỘC ---\n";

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket_fd > 0 && players[i].is_logged_in) {
            char line[64];
            snprintf(line, sizeof(line), "%s: %d điểm\n", players[i].username, players[i].score);
            strcat(msg, line);

            if (players[i].score > max_score) {
                max_score = players[i].score;
                winner_idx = i;
            }
        }
    }

    if (winner_idx != -1) {
        char winner_msg[128];
        snprintf(winner_msg, sizeof(winner_msg), "\nNGƯỜI CHIẾN THẮNG: %s !!!\n", players[winner_idx].username);
        strcat(msg, winner_msg);
    }

    broadcast(PT_GAME_END, msg);
    game_started = 0;
    current_product_idx = 0;
    
    // Reset điểm
    for(int i=0; i<MAX_CLIENTS; i++) players[i].score = 0;
}

void send_leaderboard() {
    char board[1024] = "--- BẢNG XẾP HẠNG ---\n";
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket_fd > 0 && players[i].is_logged_in) {
            char line[64];
            snprintf(line, sizeof(line), "%s: %d pts\n", players[i].username, players[i].score);
            strcat(board, line);
        }
    }
    broadcast(PT_GAME_LEADERBOARD, board);
}

void start_new_round() {
    if (current_product_idx >= product_count) {
        announce_winner();
        return;
    }
    
    // Reset bid
    for(int i=0; i<MAX_CLIENTS; i++) players[i].current_bid = -1;

    printf("Bắt đầu sản phẩm: %s\n", products[current_product_idx].name);
    
    // 1. Thông báo
    char msg[512];
    snprintf(msg, sizeof(msg), "Vòng %d/%d: %s", current_product_idx + 1, product_count, products[current_product_idx].name);
    broadcast(PT_GAME_MSG, msg);

    // 2. Gửi ảnh
    broadcast_image(products[current_product_idx].image_file);

    // 3. Gửi câu hỏi
    snprintf(msg, sizeof(msg), "Hãy nhập giá dự đoán cho: %s", products[current_product_idx].name);
    broadcast(PT_GAME_QUESTION, msg);
}

void check_round_result() {
    int actual_price = products[current_product_idx].price;
    int winner_idx = -1;
    int closest_diff = 99999999;
    
    // Kiểm tra xem tất cả mọi người đã đoán chưa
    // (Trong logic đơn giản này, ta cứ check mỗi khi có ai đó đoán. 
    // Thực tế nên chờ timer hoặc chờ đủ người)
    
    // Logic tìm người thắng tạm thời:
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket_fd > 0 && players[i].is_logged_in) {
            int bid = players[i].current_bid;
            if (bid == -1) return; // Chưa đủ người đoán -> Chờ tiếp

            if (bid <= actual_price) {
                int diff = actual_price - bid;
                if (diff < closest_diff) {
                    closest_diff = diff;
                    winner_idx = i;
                }
            }
        }
    }

    // Đã đủ người đoán -> Xử lý kết quả
    char result_msg[512];
    if (winner_idx != -1) {
        int points = (players[winner_idx].current_bid == actual_price) ? 20 : 10;
        players[winner_idx].score += points;
        snprintf(result_msg, sizeof(result_msg), 
            "KẾT QUẢ: Giá thật %d.\n%s thắng (+%d điểm) với giá %d!", 
            actual_price, players[winner_idx].username, points, players[winner_idx].current_bid);
    } else {
        snprintf(result_msg, sizeof(result_msg), 
            "KẾT QUẢ: Giá thật %d.\nKhông ai ghi điểm (tất cả đều đoán quá cao).", 
            actual_price);
    }
    
    broadcast(PT_GAME_RESULT, result_msg);
    send_leaderboard();
    
    current_product_idx++;
    sleep(3); 
    start_new_round();
}

// --- XỬ LÝ TIN NHẮN TỪ CLIENT (CORE FIX) ---
void handle_client_msg(int idx) {
    int sock = players[idx].socket_fd;
    uint32_t type_net, len_net;

    // 1. Đọc Header Type
    if (read_n(sock, &type_net, 4) <= 0) {
        printf("Client %d (%s) disconnected.\n", idx, players[idx].username);
        close(sock);
        players[idx].socket_fd = 0;
        players[idx].is_logged_in = 0;
        return;
    }

    // 2. Đọc Header Length
    if (read_n(sock, &len_net, 4) <= 0) return;

    int type = ntohl(type_net);
    int len = ntohl(len_net);

    // Sanity Check
    if (len > 1024 || len < 0) return;

    char *buf = malloc(len + 1);
    
    // 3. Đọc Payload
    if (read_n(sock, buf, len) <= 0) {
        free(buf);
        return;
    }
    buf[len] = 0;

    // 4. Xử lý logic
    if (type == PT_LOGIN) {
        // Format: "user:pass"
        char *u = strtok(buf, ":");
        char *p = strtok(NULL, ":");
        
        if (u && p && check_login(u, p) == 1) {
            strcpy(players[idx].username, u);
            players[idx].is_logged_in = 1;
            players[idx].score = 0;
            send_packet(sock, PT_LOGIN_RESP, "LOGIN_OK", strlen("LOGIN_OK"));
            
            // Debug log kiểm tra người chơi online
            int active = 0;
            printf("--- Checking Players ---\n");
            for(int i=0; i<MAX_CLIENTS; i++) {
                if(players[i].is_logged_in) {
                    active++;
                    printf("Slot %d: %s is READY\n", i, players[i].username);
                }
            }
            printf("Total Active: %d\n", active);

            // Nếu đủ người và game chưa start -> Bắt đầu
            if (active >= MIN_PLAYERS_TO_START && !game_started) {
                game_started = 1;
                start_new_round();
            } else if (!game_started) {
                char wait[100];
                snprintf(wait, sizeof(wait), "Cho nguoi choi... (%d/%d)", active, MIN_PLAYERS_TO_START);
                send_packet(sock, PT_GAME_MSG, wait, strlen(wait));
            } else {
                // Game đã start, người mới vào sẽ nhận được ảnh vòng hiện tại
                broadcast_image(products[current_product_idx].image_file);
            }
        } else {
            send_packet(sock, PT_LOGIN_RESP, "LOGIN_FAIL", strlen("LOGIN_FAIL"));
        }
    } 
    else if (type == PT_GAME_BID) {
        players[idx].current_bid = atoi(buf);
        printf("%s bid: %d\n", players[idx].username, players[idx].current_bid);
        check_round_result();
    }
    free(buf);
}

int main() {
    load_accounts();
    load_products();

    int master_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(master_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    bind(master_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(master_sock, 3);
    
    printf("Server running on port %d\n", PORT);
    
    fd_set readfds;
    int max_sd, sd;

    // Khởi tạo socket mảng players
    for(int i=0; i<MAX_CLIENTS; i++) players[i].socket_fd = 0;

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(master_sock, &readfds);
        max_sd = master_sock;

        for(int i=0; i<MAX_CLIENTS; i++) {
            sd = players[i].socket_fd;
            if(sd > 0) FD_SET(sd, &readfds);
            if(sd > max_sd) max_sd = sd;
        }

        // Blocking select
        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0 && errno != EINTR) {
            perror("Select error");
        }

        // Có kết nối mới
        if (FD_ISSET(master_sock, &readfds)) {
            int new_sock = accept(master_sock, NULL, NULL);
            if (new_sock >= 0) {
                int added = 0;
                for(int i=0; i<MAX_CLIENTS; i++) {
                    if(players[i].socket_fd == 0) {
                        players[i].socket_fd = new_sock;
                        players[i].is_logged_in = 0; // Chưa login
                        printf("New connection at slot %d\n", i);
                        added = 1;
                        break;
                    }
                }
                if (!added) close(new_sock); // Full slot
            }
        }

        // Có dữ liệu từ các Client
        for(int i=0; i<MAX_CLIENTS; i++) {
            sd = players[i].socket_fd;
            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                handle_client_msg(i);
            }
        }
    }
    return 0;
}
