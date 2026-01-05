/* client_gui.c - Price Is Right Client (Lobby + Rooms) */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

// --- UI WIDGETS ---
GtkWidget *window;
GtkWidget *stack; 

// Screen 1: Login
GtkWidget *entry_user;
GtkWidget *entry_pass;
GtkWidget *lbl_login_status;

// Screen 2: Lobby
GtkWidget *list_rooms;      // GtkTreeView
GtkListStore *store_rooms;  // Model
GtkWidget *btn_create;
GtkWidget *btn_join;

// Screen 3: Game Dashboard
GtkWidget *lbl_score;
GtkWidget *lbl_round;
GtkWidget *image_widget;
GtkWidget *lbl_overlay_status;
GtkWidget *lbl_product_name;
GtkWidget *entry_bid;
GtkWidget *text_view;
GtkTextBuffer *text_buffer;

// State
int sock;
long img_bytes_total = 0;
long img_bytes_received = 0;
FILE *img_file = NULL;
char current_img_name[128];

// --- NETWORK HELPERS ---
void send_packet(int type, const char *payload) {
    uint32_t t = htonl(type);
    uint32_t len = htonl(payload ? strlen(payload) : 0);
    send(sock, &t, 4, 0);
    send(sock, &len, 4, 0);
    if(payload) send(sock, payload, strlen(payload), 0);
}

void append_log(const char *text) {
    if (!text_buffer) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(text_buffer, &end);
    gtk_text_buffer_insert(text_buffer, &end, text, -1);
    gtk_text_buffer_insert(text_buffer, &end, "\n", -1);
    GtkTextMark *mark = gtk_text_buffer_create_mark(text_buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text_view), mark, 0.0, TRUE, 0.0, 1.0);
}

void update_image_display(const char *filename) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, 400, 300, TRUE, &error);
    if (pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image_widget), pixbuf);
        g_object_unref(pixbuf);
    }
}

// --- LOGIC ---

// Parse "id:p_count/max:state;..."
void update_lobby_list(char *data) {
    gtk_list_store_clear(store_rooms);
    char *token = strtok(data, ";");
    while(token != NULL) {
        // format: 1:1/5:WAITING
        int id;
        char status[32], count[16];
        if (sscanf(token, "%d:%15[^:]:%31s", &id, count, status) == 3) {
             GtkTreeIter iter;
             gtk_list_store_append(store_rooms, &iter);
             gtk_list_store_set(store_rooms, &iter, 0, id, 1, status, 2, count, -1);
        }
        token = strtok(NULL, ";");
    }
}

gboolean on_socket_data(GIOChannel *source, GIOCondition condition, gpointer data) {
    if (condition & (G_IO_HUP | G_IO_ERR)) return FALSE;
    
    uint32_t t_net, l_net;
    if (recv(sock, &t_net, 4, 0) <= 0) return FALSE;
    if (recv(sock, &l_net, 4, 0) <= 0) return FALSE;
    
    int type = ntohl(t_net);
    int len = ntohl(l_net);
    if(len > 1024 * 1024) return FALSE;
    
    char *buf = malloc(len + 1);
    if (len > 0) {
        int r = 0;
        while(r < len) {
            int n = recv(sock, buf + r, len - r, 0);
            if(n<=0) { free(buf); return FALSE; }
            r += n;
        }
    }
    buf[len] = 0;
    
    // Logic
    if (type == PT_LOGIN_RESP) {
        if(strcmp(buf, "OK") == 0) {
            gtk_stack_set_visible_child_name(GTK_STACK(stack), "lobby");
            gtk_label_set_text(GTK_LABEL(lbl_login_status), "Login Success");
        } else if (strcmp(buf, "REG_OK") == 0) {
            gtk_label_set_text(GTK_LABEL(lbl_login_status), "Registered! Now Login.");
        } else {
            gtk_label_set_text(GTK_LABEL(lbl_login_status), buf);
        }
    }
    else if (type == PT_LOBBY_UPDATE) {
        update_lobby_list(buf);
    }
    else if (type == PT_GAME_MSG) {
        if (strstr(buf, "Created") || strstr(buf, "joined")) {
             // Go to game screen if not already
             gtk_stack_set_visible_child_name(GTK_STACK(stack), "game");
             // Reset UI
             gtk_image_set_from_icon_name(GTK_IMAGE(image_widget), "image-missing", GTK_ICON_SIZE_DIALOG);
        }
        else if (strstr(buf, "Left")) {
             gtk_stack_set_visible_child_name(GTK_STACK(stack), "lobby");
        }
        append_log(buf);
    }
    else if (type == PT_GAME_QUESTION) {
        gtk_label_set_text(GTK_LABEL(lbl_product_name), buf);
        gtk_entry_set_text(GTK_ENTRY(entry_bid), "");
    }
    else if (type == PT_IMAGE_START) {
        char *fname = strtok(buf, ":");
        char *sz = strtok(NULL, ":");
        if(fname && sz) {
            img_bytes_total = atol(sz);
            img_bytes_received = 0;
            snprintf(current_img_name, sizeof(current_img_name), "recv_%s", fname);
            img_file = fopen(current_img_name, "wb");
        }
    }
    else if (type == PT_IMAGE_DATA) {
        if(img_file) {
            fwrite(buf, 1, len, img_file);
            img_bytes_received += len;
            if(img_bytes_received >= img_bytes_total) {
                fclose(img_file);
                img_file = NULL;
                update_image_display(current_img_name);
            }
        }
    }
    else if (type == PT_GAME_RESULT) {
        append_log(buf);
    }
    else if (type == PT_GAME_LEADERBOARD) {
        append_log(buf);
    }
    else if (type == PT_GAME_STATS) {
        int r, max, s;
        if(sscanf(buf, "%d:%d:%d", &r, &max, &s) == 3) {
            char title[32];
            snprintf(title, sizeof(title), "Round: %d/%d", r, max);
            gtk_label_set_text(GTK_LABEL(lbl_round), title);
            
            char sc[32];
            snprintf(sc, sizeof(sc), "Score: %d", s);
            gtk_label_set_text(GTK_LABEL(lbl_score), sc);
        }
    }
    free(buf);
    return TRUE;
}

// --- CALLBACKS ---
void on_connect_click() {
    char payload[128];
    snprintf(payload, sizeof(payload), "%s:%s", gtk_entry_get_text(GTK_ENTRY(entry_user)), gtk_entry_get_text(GTK_ENTRY(entry_pass)));
    send_packet(PT_LOGIN, payload);
}

void on_register_click() {
    char payload[128];
    snprintf(payload, sizeof(payload), "%s:%s", gtk_entry_get_text(GTK_ENTRY(entry_user)), gtk_entry_get_text(GTK_ENTRY(entry_pass)));
    send_packet(PT_REGISTER, payload);
}

void on_create_room_click() {
    send_packet(PT_CREATE_ROOM, "");
}

void on_join_room_click() {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(list_rooms));
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        int id;
        gtk_tree_model_get(model, &iter, 0, &id, -1);
        char buf[16]; sprintf(buf, "%d", id);
        send_packet(PT_JOIN_ROOM, buf);
    }
}

void on_leave_room_click() {
    send_packet(PT_LEAVE_ROOM, "");
}

void on_bid_click() {
    send_packet(PT_GAME_BID, gtk_entry_get_text(GTK_ENTRY(entry_bid)));
}

// --- STYLING ---
void load_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css_data =
        // Global
        "window { background-color: #f0f2f5; font-family: 'Segoe UI', 'Sans'; }"
        "button { font-weight: bold; border-radius: 6px; }"
        "entry { border-radius: 6px; padding: 8px; font-size: 14px; }"
        
        // Login Screen
        ".login-box { background-color: white; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); margin: 60px 40px; padding: 40px; }"
        ".login-title { font-size: 24px; font-weight: 800; color: #1a73e8; margin-bottom: 20px; }"
        ".btn-action { background-image: none; background-color: #1a73e8; color: white; border: none; padding: 10px; margin-top: 5px; }"
        ".btn-action:hover { background-color: #1557b0; box-shadow: 0 2px 5px rgba(26, 115, 232, 0.4); }"
        
        // Lobby Screen
        ".lobby-box { padding: 20px; }"
        ".lobby-title { font-size: 20px; font-weight: bold; color: #2c3e50; margin-bottom: 15px; border-bottom: 2px solid #3498db; padding-bottom: 5px; }"
        
        ".room-list { background-color: white; border-radius: 8px; font-size: 14px; color: #2c3e50; }"
        "treeview { background-color: white; color: #2c3e50; }"
        "treeview:selected { background-color: #3498db; color: white; }"
        
        ".btn-lobby { background-image: none; background-color: #2ecc71; color: white; padding: 10px; border: none; }"
        ".btn-lobby:hover { background-color: #27ae60; }"
        
        // Game Screen
        ".game-header { background-color: #34495e; color: white; padding: 10px; border-radius: 0 0 10px 10px; }"
        ".btn-leave { background-image: none; background-color: #e74c3c; color: white; font-size: 12px; padding: 4px 12px; border-radius: 15px; }"
        ".game-image { border: 4px solid white; border-radius: 8px; box-shadow: 0 2px 8px rgba(0,0,0,0.15); background-color: #ecf0f1; }"
        ".product-name { font-size: 18px; font-weight: bold; color: #2c3e50; margin: 10px 0; }"
        ".input-bid { font-size: 20px; color: #27ae60; font-weight: bold; padding: 10px; }"
        ".btn-bid-g { background-image: none; background-color: #f39c12; color: white; font-size: 16px; padding: 12px; border-radius: 25px; }"
        ".game-log { font-family: 'Monospace'; font-size: 11px; background-color: #2c3e50; color: #ecf0f1; padding: 8px; }";

    gtk_css_provider_load_from_data(provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void activate(GtkApplication *app, gpointer user_data) {
    load_css(); // Load styles
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Price Is Right");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 720);
    
    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_container_add(GTK_CONTAINER(window), stack);
    
    // --- 1. Login Screen ---
    GtkWidget *box_login = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_halign(box_login, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box_login, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(box_login), "login-box");
    
    GtkWidget *lbl_title = gtk_label_new("Welcome Player");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_title), "login-title");
    gtk_box_pack_start(GTK_BOX(box_login), lbl_title, FALSE, FALSE, 0);
    
    entry_user = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(entry_user), "Username");
    entry_pass = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "Password"); 
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    
    GtkWidget *btn_login = gtk_button_new_with_label("Log In");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_login), "btn-action");
    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_connect_click), NULL);
    
    GtkWidget *btn_reg = gtk_button_new_with_label("Create Account");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_reg), "btn-action");
    g_signal_connect(btn_reg, "clicked", G_CALLBACK(on_register_click), NULL);
    
    lbl_login_status = gtk_label_new("");
    
    gtk_box_pack_start(GTK_BOX(box_login), entry_user, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_login), entry_pass, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_login), btn_login, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_login), btn_reg, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_login), lbl_login_status, FALSE, FALSE, 10);
    
    gtk_stack_add_named(GTK_STACK(stack), box_login, "login");
    
    // --- 2. Lobby Screen ---
    GtkWidget *box_lobby = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(box_lobby), "lobby-box");
    
    GtkWidget *lbl_lobby = gtk_label_new("Available Rooms");
    gtk_widget_set_halign(lbl_lobby, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_lobby), "lobby-title");
    gtk_box_pack_start(GTK_BOX(box_lobby), lbl_lobby, FALSE, FALSE, 0);
    
    store_rooms = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    list_rooms = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store_rooms));
    gtk_style_context_add_class(gtk_widget_get_style_context(list_rooms), "room-list");
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_rooms), -1, "ID", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_rooms), -1, "Status", renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_rooms), -1, "Players", renderer, "text", 2, NULL);
    
    GtkWidget *scroll_lobby = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll_lobby), list_rooms);
    gtk_box_pack_start(GTK_BOX(box_lobby), scroll_lobby, TRUE, TRUE, 0);
    
    GtkWidget *lobby_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    btn_create = gtk_button_new_with_label("+ Create Room");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_create), "btn-lobby");
    g_signal_connect(btn_create, "clicked", G_CALLBACK(on_create_room_click), NULL);
    
    btn_join = gtk_button_new_with_label("Join Selected ->");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_join), "btn-lobby");
    g_signal_connect(btn_join, "clicked", G_CALLBACK(on_join_room_click), NULL);
    
    gtk_box_pack_start(GTK_BOX(lobby_actions), btn_create, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(lobby_actions), btn_join, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_lobby), lobby_actions, FALSE, FALSE, 5);
    
    gtk_stack_add_named(GTK_STACK(stack), box_lobby, "lobby");
    
    // --- 3. Game Screen ---
    GtkWidget *box_game = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    // Header
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(head), "game-header");
    
    GtkWidget *lbl_game_title = gtk_label_new("Game Room");
    lbl_round = gtk_label_new("Round: -/-");
    lbl_score = gtk_label_new("Score: 0");
    
    GtkWidget *btn_leave = gtk_button_new_with_label("Leave");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_leave), "btn-leave");
    g_signal_connect(btn_leave, "clicked", G_CALLBACK(on_leave_room_click), NULL);
    
    gtk_box_pack_start(GTK_BOX(head), lbl_game_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(head), lbl_round, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(head), lbl_score, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(head), btn_leave, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_game), head, FALSE, FALSE, 0);
    
    // Stage
    GtkWidget *stage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(stage), 15);
    
    image_widget = gtk_image_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(image_widget), "game-image");
    gtk_box_pack_start(GTK_BOX(stage), image_widget, TRUE, TRUE, 0);
    
    lbl_product_name = gtk_label_new("Waiting for next round...");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_product_name), "product-name");
    gtk_box_pack_start(GTK_BOX(stage), lbl_product_name, FALSE, FALSE, 5);
    
    gtk_box_pack_start(GTK_BOX(box_game), stage, TRUE, TRUE, 0);
    
    // Control Deck
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(controls), 20);
    
    entry_bid = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_bid), "Enter Price (.000VND)");
    gtk_style_context_add_class(gtk_widget_get_style_context(entry_bid), "input-bid");
    gtk_entry_set_alignment(GTK_ENTRY(entry_bid), 0.5);
    
    GtkWidget *btn_bid_g = gtk_button_new_with_label("PLACE BID");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_bid_g), "btn-bid-g");
    g_signal_connect(btn_bid_g, "clicked", G_CALLBACK(on_bid_click), NULL);
    
    gtk_box_pack_start(GTK_BOX(controls), entry_bid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), btn_bid_g, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_game), controls, FALSE, FALSE, 0);
    
    // Log
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 120);
    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(text_view), "game-log");
    text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_container_add(GTK_CONTAINER(scroll), text_view);
    
    GtkWidget *expander = gtk_expander_new("Game Log");
    gtk_container_add(GTK_CONTAINER(expander), scroll);
    gtk_box_pack_end(GTK_BOX(box_game), expander, FALSE, FALSE, 5);
    
    gtk_stack_add_named(GTK_STACK(stack), box_game, "game");
    
    gtk_widget_show_all(window);
    
    GIOChannel *io = g_io_channel_unix_new(sock);
    g_io_add_watch(io, G_IO_IN|G_IO_HUP|G_IO_ERR, on_socket_data, NULL);
}

int main(int argc, char *argv[]) {
    if(argc != 3) return 1;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in s;
    s.sin_family=AF_INET; s.sin_port=htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &s.sin_addr);
    connect(sock, (struct sockaddr*)&s, sizeof(s));
    
    GtkApplication *app = gtk_application_new("org.price.game", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_application_run(G_APPLICATION(app), 0, NULL);
}
