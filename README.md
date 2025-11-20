The Price Is Right (Hãy Chọn Giá Đúng) - Network Game

Dự án game show qua mạng LAN sử dụng C Socket, kiến trúc I/O Multiplexing (select) và giao diện GTK+ 3.

📸 Tính năng

Server: Quản lý nhiều người chơi, tính điểm, bảng xếp hạng, gửi ảnh sản phẩm.

Client: - Giao diện đồ họa (GUI) viết bằng GTK+ 3.

Hiển thị ảnh sản phẩm tự động.

Chat/Đấu giá thời gian thực.

🛠 Yêu cầu hệ thống (Dependencies)

Để chạy được dự án, bạn cần cài đặt các công cụ sau trên Linux (Ubuntu/WSL):

1. Trình biên dịch & Thư viện cơ bản

sudo apt update
sudo apt install build-essential


2. Thư viện giao diện GTK+ 3 (Cho Client GUI)

sudo apt install libgtk-3-dev


3. Trình xem ảnh (Tùy chọn cho Client Console)

Nếu bạn dùng bản Client dòng lệnh, cần cài feh hoặc xdg-open:

sudo apt install feh


🚀 Cách cài đặt và chạy

Bước 1: Clone dự án

git clone [https://github.com/username-cua-ban/price-is-right.git](https://github.com/username-cua-ban/price-is-right.git)
cd price-is-right


Bước 2: Biên dịch

Sử dụng Makefile đã tích hợp sẵn:

make


Bước 3: Chạy Server

./server_game


Lưu ý: Server cần file account.txt và products.txt cùng các file ảnh sản phẩm ở cùng thư mục.

Bước 4: Chạy Client

Mở terminal mới và chạy (thay IP bằng IP máy Server):

./client_gui 127.0.0.1 5500


📂 Cấu trúc thư mục

server_game.c: Mã nguồn Server.

client_gui.c: Mã nguồn Client giao diện GTK.

client_game.c: Mã nguồn Client dòng lệnh (Backup).

common.h: Định nghĩa gói tin dùng chung.

account.txt: Danh sách tài khoản (user pass status).

products.txt: Danh sách sản phẩm.