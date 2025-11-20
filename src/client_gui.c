/* client_gui.c - The Price is Right Client (GTK+ 3 GUI) - FIXED MULTI-INSTANCE */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

// --- UI WIDGETS ---
GtkWidget *window;
GtkWidget *image_widget;
GtkWidget *text_view;
GtkWidget *entry_input;
GtkWidget *btn_send;
GtkTextBuffer *text_buffer;

// --- NETWORK GLOBALS ---
int sock;
int receiving_img = 0;
long img_bytes_total = 0;
long img_bytes_received = 0;
FILE *img_file = NULL;
char current_img_name[128];

// --- HELPER: APPEND TEXT TO LOG ---
void append_log(const char *text) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(text_buffer, &end);
    gtk_text_buffer_insert(text_buffer, &end, text, -1);
    gtk_text_buffer_insert(text_buffer, &end, "\n", -1);
    
    // Auto scroll to bottom
    GtkTextMark *mark = gtk_text_buffer_create_mark(text_buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text_view), mark, 0.0, TRUE, 0.0, 1.0);
}

void send_packet(int fd, int type, char *payload) {
    uint32_t t = htonl(type);
    uint32_t len = htonl(strlen(payload));
    if (send(fd, &t, 4, 0) < 0) return;
    if (send(fd, &len, 4, 0) < 0) return;
    send(fd, payload, strlen(payload), 0);
}

// --- NETWORK HANDLER (FIXED) ---
gboolean on_socket_data(GIOChannel *source, GIOCondition condition, gpointer data) {
    // 1. Kiểm tra lỗi hoặc ngắt kết nối
    if (condition & (G_IO_HUP | G_IO_ERR)) {
        append_log("[SYSTEM] Mất kết nối tới Server.");
        return FALSE; 
    }

    // 2. Đọc Header (Type)
    uint32_t type_net, len_net;
    ssize_t r1 = recv(sock, &type_net, 4, 0);
    if (r1 <= 0) {
        append_log("[SYSTEM] Server đã đóng kết nối.");
        return FALSE;
    }

    // 3. Đọc Header (Length)
    ssize_t r2 = recv(sock, &len_net, 4, 0);
    if (r2 <= 0) return FALSE;

    int type = ntohl(type_net);
    int len = ntohl(len_net);
    
    if (len > 10000000 || len < 0) {
        append_log("[ERROR] Gói tin rác hoặc quá lớn.");
        return FALSE;
    }

    char *buf = malloc(len + 1);
    if (!buf) return FALSE;

    // 4. Đọc Payload
    int received = 0;
    while (received < len) {
        int n = recv(sock, buf + received, len - received, 0);
        if (n <= 0) {
            free(buf);
            return FALSE;
        }
        received += n;
    }
    buf[len] = 0;

    // --- XỬ LÝ GÓI TIN ---
    if (type == PT_LOGIN_RESP) {
        if (strcmp(buf, "LOGIN_FAIL") == 0) {
            append_log("[SYSTEM] Đăng nhập thất bại! Sai tài khoản hoặc bị khóa.");
        } else {
            append_log("[SYSTEM] Đăng nhập thành công! Đang chờ game...");
        }
    }
    else if (type == PT_GAME_MSG) {
        char msg[512];
        snprintf(msg, sizeof(msg), "[THÔNG BÁO] %s", buf);
        append_log(msg);
    }
    else if (type == PT_GAME_QUESTION) {
        append_log("\n--- CÂU HỎI MỚI ---");
        append_log(buf);
        append_log(">>> Hãy nhập giá dự đoán vào ô bên dưới:");
    }
    else if (type == PT_GAME_RESULT) {
        append_log("\n[KẾT QUẢ]");
        append_log(buf);
    }
    else if (type == PT_GAME_LEADERBOARD) {
        append_log(buf);
    }
    else if (type == PT_GAME_END) {
        append_log("\n=== TRÒ CHƠI KẾT THÚC ===");
        append_log(buf);
    }
    else if (type == PT_IMAGE_START) {
        char *fname = strtok(buf, ":");
        char *sz = strtok(NULL, ":");
        if (fname && sz) {
            img_bytes_total = atol(sz);
            img_bytes_received = 0;
            snprintf(current_img_name, sizeof(current_img_name), "gui_recv_%s", fname);
            img_file = fopen(current_img_name, "wb");
            if (img_file) {
                char log[128];
                snprintf(log, sizeof(log), "[IMAGE] Đang tải ảnh: %s...", fname);
                append_log(log);
            }
        }
    }
    else if (type == PT_IMAGE_DATA) {
        if (img_file) {
            fwrite(buf, 1, len, img_file);
            img_bytes_received += len;
            
            if (img_bytes_received >= img_bytes_total) {
                fclose(img_file);
                img_file = NULL;
                append_log("[IMAGE] Đã tải xong. Hiển thị ảnh.");
                gtk_image_set_from_file(GTK_IMAGE(image_widget), current_img_name);
            }
        }
    }

    free(buf);
    return TRUE;
}

// --- UI CALLBACKS ---
void on_send_clicked(GtkWidget *widget, gpointer data) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry_input));
    if (strlen(text) == 0) return;

    if (strchr(text, ':')) {
        send_packet(sock, PT_LOGIN, (char*)text);
        append_log("-> Đang gửi thông tin đăng nhập...");
    } else {
        send_packet(sock, PT_GAME_BID, (char*)text);
        char log[100];
        snprintf(log, sizeof(log), "-> Bạn đoán: %s", text);
        append_log(log);
    }

    gtk_entry_set_text(GTK_ENTRY(entry_input), "");
}

void on_entry_activate(GtkWidget *widget, gpointer data) {
    on_send_clicked(NULL, NULL);
}

// --- MAIN SETUP ---
void activate(GtkApplication *app, gpointer user_data) {
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "The Price Is Right - Client");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 750);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), box);

    image_widget = gtk_image_new();
    gtk_image_set_from_icon_name(GTK_IMAGE(image_widget), "image-missing", GTK_ICON_SIZE_DIALOG);
    gtk_box_pack_start(GTK_BOX(box), image_widget, FALSE, FALSE, 10);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled_window, TRUE); 
    gtk_box_pack_start(GTK_BOX(box), scrolled_window, TRUE, TRUE, 0);

    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
    text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

    GtkWidget *hbox_input = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(box), hbox_input, FALSE, FALSE, 10);

    entry_input = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_input), "Nhập 'user:pass' để login hoặc nhập giá...");
    g_signal_connect(entry_input, "activate", G_CALLBACK(on_entry_activate), NULL);
    gtk_box_pack_start(GTK_BOX(hbox_input), entry_input, TRUE, TRUE, 5);

    btn_send = gtk_button_new_with_label("Gửi");
    g_signal_connect(btn_send, "clicked", G_CALLBACK(on_send_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox_input), btn_send, FALSE, FALSE, 5);

    append_log("Chào mừng đến với Hãy Chọn Giá Đúng!");
    append_log("Vui lòng nhập 'username:password' để đăng nhập.");

    gtk_widget_show_all(window);

    GIOChannel *io_channel = g_io_channel_unix_new(sock);
    g_io_add_watch(io_channel, G_IO_IN | G_IO_HUP | G_IO_ERR, on_socket_data, NULL);
    g_io_channel_unref(io_channel);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <IP> <PORT>\n", argv[0]);
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Cannot connect to server.\n");
        return 1;
    }

    GtkApplication *app;
    int status;

    // SỬA LỖI QUAN TRỌNG: Dùng G_APPLICATION_NON_UNIQUE để cho phép mở nhiều Client
    app = gtk_application_new("org.example.pricegame", G_APPLICATION_NON_UNIQUE);
    
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    close(sock);
    return status;
}
