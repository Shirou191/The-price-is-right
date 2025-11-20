/* client_game.c - Client with Image Popup using 'feh' */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "common.h"

// Biến toàn cục quản lý nhận ảnh
int receiving_img = 0;
long img_bytes_total = 0;
long img_bytes_received = 0;
FILE *img_file = NULL;
char current_img_name[128];

// Hàm mở ảnh (Pop up) sử dụng 'feh'
void open_image_viewer(char *filename) {
    char command[256];
    
    // Sử dụng feh để mở ảnh
    // Tham số -. (hoặc --scale-down) để tự động thu nhỏ ảnh nếu to quá màn hình
    // Dấu & ở cuối để chạy nền, không bị treo game
    snprintf(command, sizeof(command), "feh -. %s &", filename);
    
    printf("[SYSTEM] Dang mo anh bang feh: %s\n", filename);
    system(command);
}

void send_packet(int fd, int type, char *payload) {
    uint32_t t = htonl(type);
    uint32_t len = htonl(strlen(payload));
    send(fd, &t, 4, 0);
    send(fd, &len, 4, 0);
    send(fd, payload, strlen(payload), 0);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: ./client_game <IP> <PORT>\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connect fail.\n");
        return 1;
    }

    // Đăng nhập
    char user[64], pass[64], login_payload[128];
    printf("Username: "); 
    if(!fgets(user, 64, stdin)) return 0; user[strcspn(user, "\n")] = 0;
    printf("Password: ");
    if(!fgets(pass, 64, stdin)) return 0; pass[strcspn(pass, "\n")] = 0;
    
    snprintf(login_payload, sizeof(login_payload), "%s:%s", user, pass);
    send_packet(sock, PT_LOGIN, login_payload);

    fd_set readfds;
    printf("=== CHOI GAME: HAY CHON GIA DUNG ===\n");

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        // Chờ sự kiện
        select(sock + 1, &readfds, NULL, NULL, NULL);

        // 1. Có dữ liệu từ Server
        if (FD_ISSET(sock, &readfds)) {
            uint32_t type_net, len_net;
            if (recv(sock, &type_net, 4, 0) <= 0) break;
            if (recv(sock, &len_net, 4, 0) <= 0) break;
            
            int type = ntohl(type_net);
            int len = ntohl(len_net);

            char *buf = malloc(len + 1);
            int r = 0;
            while(r < len) {
                int n = recv(sock, buf + r, len - r, 0);
                if(n <= 0) break;
                r += n;
            }
            buf[len] = 0;

            if (type == PT_LOGIN_RESP) {
                if (strcmp(buf, "LOGIN_FAIL") == 0) {
                    printf("Sai tai khoan/mat khau.\n");
                    return 0;
                }
                printf("Dang nhap thanh cong. Cho Server...\n");
            } 
            else if (type == PT_GAME_MSG) {
                printf("\n[THONG BAO] %s\n", buf);
            }
            else if (type == PT_GAME_QUESTION) {
                printf("\n--------------------------------\n");
                printf("[CAU HOI] %s\n", buf);
                printf("Nhap gia du doan: ");
                fflush(stdout);
            }
            else if (type == PT_GAME_RESULT) {
                printf("\n[KET QUA] %s\n", buf);
            }
            else if (type == PT_GAME_LEADERBOARD) {
                printf("\n%s\n", buf);
            }
            else if (type == PT_GAME_END) {
                printf("\n================================\n");
                printf("%s", buf);
                printf("================================\n");
                return 0;
            }
            else if (type == PT_IMAGE_START) {
                char *fname = strtok(buf, ":");
                char *sz = strtok(NULL, ":");
                img_bytes_total = atol(sz);
                img_bytes_received = 0;
                
                snprintf(current_img_name, sizeof(current_img_name), "recv_%s", fname);
                
                img_file = fopen(current_img_name, "wb");
                if(img_file) {
                    printf("\n[IMAGE] Dang tai anh: %s (%ld bytes)...\n", fname, img_bytes_total);
                }
            }
            else if (type == PT_IMAGE_DATA) {
                if (img_file) {
                    fwrite(buf, 1, len, img_file);
                    img_bytes_received += len;

                    // KHI NHẬN XONG
                    if (img_bytes_received >= img_bytes_total) {
                        fclose(img_file);
                        img_file = NULL;
                        printf("[IMAGE] Tai xong. Bat len bang feh...\n");
                        
                        // GỌI HÀM MỞ ẢNH TẠI ĐÂY
                        open_image_viewer(current_img_name);
                    }
                }
            }
            
            free(buf);
        }

        // 2. Người dùng nhập từ bàn phím
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[64];
            if(fgets(input, 64, stdin) == NULL) break;
            input[strcspn(input, "\n")] = 0;
            send_packet(sock, PT_GAME_BID, input);
        }
    }
    close(sock);
    return 0;
}
