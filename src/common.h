#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

// Packet Types
enum {
    PT_LOGIN = 1,
    PT_LOGIN_RESP = 2,
    PT_GAME_MSG = 3,        // Thông báo chung
    PT_GAME_QUESTION = 4,   // Tên sản phẩm & bắt đầu vòng
    PT_GAME_BID = 5,        // Client gửi giá
    PT_GAME_RESULT = 6,     // Kết quả vòng chơi
    PT_GAME_LEADERBOARD = 7,// Bảng xếp hạng
    PT_IMAGE_START = 8,     // Bắt đầu gửi ảnh (Header: Tên file, Kích thước)
    PT_IMAGE_DATA = 9,      // Dữ liệu ảnh
    PT_GAME_END = 10        // Kết thúc game
};

// Cấu trúc sản phẩm
typedef struct {
    char name[100];
    int price;
    char image_file[100];
} Product;

#endif
