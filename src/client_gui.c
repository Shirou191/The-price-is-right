/* client_gui.c - Price Is Right Client (Lobby + Rooms) */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h> /* NEW */
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
GtkWidget *list_online_players; /* NEW: Lobby Player List */
GtkListStore *store_players_online; /* NEW: Model for players */
GtkWidget *lbl_lobby_user; /* NEW */
GtkWidget *btn_create;
GtkWidget *btn_join;
GtkWidget *btn_replay; /* NEW */
GtkWidget *lbl_empty_rooms; /* NEW: Empty State */

// Screen 3: Game Dashboard
GtkWidget *lbl_score;
GtkWidget *lbl_round;
GtkWidget *lbl_game_title; /* NEW: Global for Replay Mode title change */
GtkWidget *lbl_game_user; /* NEW */
GtkWidget *image_widget;
GtkWidget *lbl_overlay_status;
GtkWidget *lbl_product_name;
GtkWidget *entry_bid;
GtkWidget *btn_start_game; /* NEW */
GtkWidget *btn_invite; /* NEW */
GtkWidget *text_view;
GtkTextBuffer *text_buffer;

// State
int sock;
long img_bytes_total = 0;
long img_bytes_received = 0;
FILE *img_file = NULL;
char current_img_name[128];
char my_username[64] = ""; /* NEW */
int is_owner = 0; /* NEW */
FILE *replay_log = NULL; /* NEW */
GList *replay_lines = NULL; /* NEW: Replay buffer */
guint replay_timer_id = 0; /* NEW: Replay timer */
// Timer
GtkWidget *lbl_timer; /* NEW */
guint round_timer_id = 0;
int round_time_left = 30;

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
    
    // Write to Replay File
    if (replay_log) {
        fprintf(replay_log, "%s\n", text);
        fflush(replay_log);
    }

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
    
    // Check Empty State
    if(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store_rooms), NULL) == 0) {
        gtk_widget_set_visible(lbl_empty_rooms, TRUE);
    } else {
        gtk_widget_set_visible(lbl_empty_rooms, FALSE);
    }
}

// Timer Logic
gboolean on_round_timer(gpointer data) {
    round_time_left--;
    char buf[32];
    snprintf(buf, sizeof(buf), "Time: %ds", round_time_left);
    gtk_label_set_text(GTK_LABEL(lbl_timer), buf);
    
    if (round_time_left <= 0) {
        round_timer_id = 0;
        return FALSE; // Stop
    }
    return TRUE;
}

void start_round_timer() {
    if(round_timer_id > 0) g_source_remove(round_timer_id);
    round_time_left = 30;
    gtk_label_set_text(GTK_LABEL(lbl_timer), "Time: 30s");
    round_timer_id = g_timeout_add(1000, on_round_timer, NULL);
}

void stop_round_timer() {
    if(round_timer_id > 0) {
        g_source_remove(round_timer_id);
        round_timer_id = 0;
    }
    gtk_label_set_text(GTK_LABEL(lbl_timer), "Time: --");
}

// Toast Helper
gboolean on_toast_timeout(gpointer data) {
    gtk_widget_destroy(GTK_WIDGET(data));
    return FALSE;
}

void show_toast(const char *msg) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_POPUP);
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "toast-box"); /* NEW */
    gtk_container_add(GTK_CONTAINER(win), frame);
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 20); // Padding
    gtk_container_add(GTK_CONTAINER(frame), box);
    
    GtkWidget *lbl = gtk_label_new(msg);
    gtk_box_pack_start(GTK_BOX(box), lbl, TRUE, TRUE, 0);
    
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(window));
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    
    gtk_widget_show_all(win);
    g_timeout_add(2000, on_toast_timeout, win);
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
            
            char u_msg[128];
            snprintf(u_msg, sizeof(u_msg), "User: %s", my_username);
            gtk_label_set_text(GTK_LABEL(lbl_lobby_user), u_msg);
            gtk_label_set_text(GTK_LABEL(lbl_game_user), u_msg);
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

             // Start Recording Log
             // Start Recording Log
             if(!replay_log) { // Only open if not already recording
                 char mkdir_cmd[128];
                 snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p replays/%s", my_username);
                 system(mkdir_cmd);

                 char fname[256];
                 time_t now = time(NULL);
                 snprintf(fname, sizeof(fname), "replays/%s/log_%ld.txt", my_username, now);
                 replay_log = fopen(fname, "w");
                 if(replay_log) {
                     fprintf(replay_log, "--- Start Log ---\n");
                     printf("DEBUG: Recording to %s\n", fname);
                     fflush(stdout);
                 } else {
                     printf("DEBUG: Failed to open %s\n", fname);
                     fflush(stdout);
                 }
             }
        }
        
        if (strstr(buf, "Owner")) {
             is_owner = 1;
             gtk_widget_set_visible(btn_start_game, TRUE); // Show for owner
        }
        
        // Ensure button visibility is correct (persist if owner)
        gtk_widget_set_visible(btn_start_game, is_owner);

        if (strstr(buf, "Left")) {
             is_owner = 0;
             gtk_stack_set_visible_child_name(GTK_STACK(stack), "lobby");
             gtk_widget_set_visible(btn_start_game, FALSE);
             
             // Stop Recording
             if(replay_log) {
                 fprintf(replay_log, "--- End Log ---\n");
                 fclose(replay_log);
                 replay_log = NULL;
                 printf("DEBUG: Recording stopped.\n");
                 fflush(stdout);
             }
        }
        
        // Invite Feedback Dialogs
        if (strstr(buf, "busy") || strstr(buf, "not found") || strstr(buf, "declined") || strstr(buf, "full")) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_WARNING,
                                     GTK_BUTTONS_OK,
                                     "%s", buf);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        } else if (strstr(buf, "Invite sent")) {
             GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_INFO,
                                     GTK_BUTTONS_OK,
                                     "%s", buf);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        } else if (strstr(buf, "You earned") || strstr(buf, "You overbid!")) {
             show_toast(buf);
        }
        
        append_log(buf);
    }
    else if (type == PT_INVITE) {
        // buf = "RoomID:SenderName"
        char *r_id_s = strtok(buf, ":");
        char *sender = strtok(NULL, ":");
        
        if (sender) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_YES_NO,
                                     "%s invites you to Room %s. Join?", sender, r_id_s);
            gint result = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            
            char resp[64];
            snprintf(resp, sizeof(resp), "%s:%s", sender, (result == GTK_RESPONSE_YES) ? "ACCEPT" : "DECLINE");
            send_packet(PT_INVITE_RESP, resp);
        }
    }
    else if (type == PT_GAME_END) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_INFO,
                                     GTK_BUTTONS_OK,
                                     "%s", buf);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
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
            
            // Start Timer on new round image
            start_round_timer();
            
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
                
                // Log Image Event
                if(replay_log) {
                    fprintf(replay_log, "<<<IMG:%s>>>\n", current_img_name);
                    fflush(replay_log);
                }
            }
        }
    }
    else if (type == PT_GAME_RESULT) {
        stop_round_timer();
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
            
            // Log Stats
            if(replay_log) {
                fprintf(replay_log, "<<<STATS:%s>>>\n", buf);
                fflush(replay_log);
            }
        }
    }
    else if (type == PT_PLAYER_LIST) {
        gtk_list_store_clear(store_players_online);
        char *token = strtok(buf, ";");
        while(token) {
            // "Name:RoomID"
            char name[64];
            int rid;
            if(sscanf(token, "%63[^:]:%d", name, &rid) == 2) {
                // If invite dialog needs this list, it uses this store.
                char status[32];
                if(rid == 0) strcpy(status, "Lobby");
                else sprintf(status, "Room %d", rid);
                
                GtkTreeIter iter;
                gtk_list_store_append(store_players_online, &iter);
                gtk_list_store_set(store_players_online, &iter, 0, name, 1, status, -1);
            }
            token = strtok(NULL, ";");
        }
    }
    free(buf);
    return TRUE;
}

// --- CALLBACKS ---
void on_connect_click() {
    char payload[128];
    const char *u = gtk_entry_get_text(GTK_ENTRY(entry_user));
    snprintf(payload, sizeof(payload), "%s:%s", u, gtk_entry_get_text(GTK_ENTRY(entry_pass)));
    strncpy(my_username, u, sizeof(my_username)-1); /* Store Username */
    printf("DEBUG: Username set to '%s'\n", my_username);
    fflush(stdout);
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

// --- REPLAY SYSTEM ---

gboolean replay_step(gpointer data) {
    if (!replay_lines) {
        append_log("--- Replay Finished ---");
        replay_timer_id = 0;
        return FALSE; // Stop timer
    }
    
    char *line = (char*)replay_lines->data;
    
    // Check for Image Tag
    if (strncmp(line, "<<<IMG:", 7) == 0) {
        // Format: <<<IMG:filename>>>
        char *end = strchr(line, '>');
        if(end) {
            *end = 0;
            char *fname = line + 7;
            update_image_display(fname);
        }
    } else if (strncmp(line, "<<<BID:", 7) == 0) {
        // Format: <<<BID:value>>>
        char *end = strchr(line, '>');
        if(end) {
            *end = 0;
            char *val = line + 7;
            gtk_entry_set_text(GTK_ENTRY(entry_bid), val);
        }
    } else if (strncmp(line, "<<<STATS:", 9) == 0) {
        // Format: <<<STATS:r:m:s>>>
        char *end = strchr(line, '>');
        if(end) *end = 0;
        char *stats = line + 9;
        int r, max, s;
        if(sscanf(stats, "%d:%d:%d", &r, &max, &s) == 3) {
            char title[32];
            snprintf(title, sizeof(title), "Round: %d/%d", r, max);
            gtk_label_set_text(GTK_LABEL(lbl_round), title);
            char sc[32];
            snprintf(sc, sizeof(sc), "Score: %d", s);
            gtk_label_set_text(GTK_LABEL(lbl_score), sc);
        }
    } else {
        if(strstr(line, "You earned")) {
            show_toast(line);
        }
        append_log(line);
    }
    
    g_free(line);
    
    replay_lines = g_list_delete_link(replay_lines, replay_lines);
    return TRUE; // Continue
}

void start_replay(const char *filename) {
    if(replay_timer_id > 0) g_source_remove(replay_timer_id);
    if(replay_lines) { g_list_free_full(replay_lines, g_free); replay_lines = NULL; }
    
    FILE *f = fopen(filename, "r");
    if(!f) return;
    
    char buf[1024];
    while(fgets(buf, sizeof(buf), f)) {
        // Strip newline
        buf[strcspn(buf, "\n")] = 0;
        replay_lines = g_list_append(replay_lines, g_strdup(buf));
    }
    fclose(f);
    
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "game");
    gtk_text_buffer_set_text(text_buffer, "", -1); // Clear Log
    gtk_label_set_text(GTK_LABEL(lbl_game_title), "REPLAY MODE");
    append_log("--- Starting Replay in 1s ---");
    
    // Disable Game Controls (Show but disable)
    // Disable Game Controls
    gtk_widget_set_sensitive(entry_bid, FALSE);
    gtk_widget_set_visible(btn_invite, FALSE); // Hide Invite
    gtk_widget_set_visible(btn_start_game, FALSE); // Start button still hidden (logic)
    
    replay_timer_id = g_timeout_add(1000, replay_step, NULL);
}

void on_replay_click() {
    GtkWidget *dialog;
    dialog = gtk_file_chooser_dialog_new("Open Replay",
                                         GTK_WINDOW(window),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         "Cancel", GTK_RESPONSE_CANCEL,
                                         "Open", GTK_RESPONSE_ACCEPT,
                                         NULL);
                                         
    // Set current folder to replays/USERNAME
    char path[512];
    getcwd(path, sizeof(path));
    snprintf(path + strlen(path), sizeof(path) - strlen(path), "/replays/%s", my_username);
    
    // Ensure it exists so chooser doesn't complain
    char mkdir_cmd[550]; snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", path); system(mkdir_cmd);
    
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), path);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        start_replay(filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
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

void on_leave_click() {
    // Stop Replay if active
    if(replay_timer_id > 0) {
        g_source_remove(replay_timer_id);
        replay_timer_id = 0;
    }
    if(replay_lines) {
        g_list_free_full(replay_lines, g_free);
        replay_lines = NULL;
    }
    
    // If we were replaying, just go back to lobby locally
    if(replay_timer_id == 0 && !replay_lines && strcmp(gtk_label_get_text(GTK_LABEL(lbl_game_title)), "REPLAY MODE") == 0) {
        // We just stopped replay
        gtk_stack_set_visible_child_name(GTK_STACK(stack), "lobby");
        gtk_image_set_from_icon_name(GTK_IMAGE(image_widget), "image-missing", GTK_ICON_SIZE_DIALOG);
        
        // Re-enable controls
        gtk_widget_set_sensitive(entry_bid, TRUE);
        gtk_widget_set_visible(btn_invite, TRUE); // Show Invite
        gtk_widget_set_sensitive(btn_invite, TRUE);
        gtk_label_set_text(GTK_LABEL(lbl_game_title), "Game Room");
        return;
    }
    
    // Re-enable controls
    gtk_widget_set_sensitive(entry_bid, TRUE);
    gtk_widget_set_visible(btn_invite, TRUE);
    gtk_widget_set_sensitive(btn_invite, TRUE);
    gtk_label_set_text(GTK_LABEL(lbl_game_title), "Game Room"); // Reset Title

    send_packet(PT_LEAVE_ROOM, "");
}

void on_bid_click() {
    const char *bid_text = gtk_entry_get_text(GTK_ENTRY(entry_bid));
    if(strlen(bid_text) > 0) {
        send_packet(PT_GAME_BID, bid_text);
        gtk_entry_set_text(GTK_ENTRY(entry_bid), "");
        stop_round_timer(); /* Stop Timer on Bid */
        
        // Log Bid
        if(replay_log) {
            fprintf(replay_log, "<<<BID:%s>>>\n", bid_text);
            fflush(replay_log);
        }
    }
}

void on_start_click() {
    send_packet(PT_START_GAME, "");
}

void on_invite_click() {
    GtkWidget *dialog, *content_area;
    GtkWidget *combo;
    GtkCellRenderer *renderer;
    
    dialog = gtk_dialog_new_with_buttons("Invite Player", GTK_WINDOW(window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Cancel", GTK_RESPONSE_CANCEL,
                                         "Invite", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    // Use Dropdown with Filtered List
    GtkListStore *temp_store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store_players_online), &iter);
    while(valid) {
        char *name;
        char *status;
        gtk_tree_model_get(GTK_TREE_MODEL(store_players_online), &iter, 0, &name, 1, &status, -1);
        
        // Only add if not myself (and maybe only if in Lobby? User didn't ask for that but good UX. User only asked "not myself")
        // Let's just filter myself for now as requested.
        if (name && strcmp(name, my_username) != 0) {
            char display[100];
            snprintf(display, sizeof(display), "%s (%s)", name, status); // Show status in dropdown
            GtkTreeIter ti;
            gtk_list_store_append(temp_store, &ti);
            gtk_list_store_set(temp_store, &ti, 0, display, -1);
        }
        if(name) g_free(name);
        if(status) g_free(status);
        
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store_players_online), &iter);
    }

    combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(temp_store));
    g_object_unref(temp_store); // Combo holds ref now
    
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), renderer, "text", 0, NULL);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    
    gtk_box_pack_start(GTK_BOX(content_area), gtk_label_new("Select Player:"), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content_area), combo, TRUE, TRUE, 5);
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTreeIter iter;
        if(gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter)) {
            char *display;
            gtk_tree_model_get(GTK_TREE_MODEL(temp_store), &iter, 0, &display, -1);
            // Parse "Name (Status)" -> just Name
            char *sp = strchr(display, ' ');
            if(sp) *sp = 0;
            
            if(display && strlen(display) > 0) {
                 send_packet(PT_INVITE, display);
            }
            g_free(display);
        }
    }
    gtk_widget_destroy(dialog);
}

// --- STYLING ---
void load_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css_data =
        // Refined Golden & Red Theme (Luxurious Game Show)
        "headerbar { background-color: #D50000; border-bottom: 2px solid #FFD700; min-height: 48px; }"
        "headerbar label { color: #FFD700; font-weight: 900; font-size: 20px; text-shadow: 1px 1px 2px #000; }"
        
        "window { background-color: #2B0505; color: #FFF8E1; font-family: 'Segoe UI', 'Roboto', 'Sans'; }"
        
        // --- BUTTON HIERARCHY ---
        // Base Button Reset
        "button { background-image: none; border: none; border-radius: 20px; padding: 10px 20px; font-weight: 900; -gtk-icon-shadow: none; }"
        "button:disabled { background-color: #3E2723; color: #6D4C41; box-shadow: none; }"

        // Primary Action (Log In, Create Room, Place Bid) - Gradient Gold, Strong Shadow
        ".btn-primary { "
        "   background-image: linear-gradient(to bottom, #FFD700, #FFC107); "
        "   color: #3E2723; "
        "   border: 1px solid #FFEA00; "
        "   box-shadow: 0 4px 6px rgba(0,0,0,0.4); "
        "}"
        ".btn-primary:hover { "
        "   background-image: linear-gradient(to bottom, #FFEA00, #FFD54F); "
        "   box-shadow: 0 6px 8px rgba(0,0,0,0.5); "
        "}"
        ".btn-primary:active { "
        "   background-image: linear-gradient(to bottom, #FFC107, #FFB300); "
        "   box-shadow: inset 0 2px 4px rgba(0,0,0,0.3); "
        "}"
        
        // Secondary Action (Join Room, Reg) - Outlined Gold
        ".btn-secondary { "
        "   background-color: transparent; "
        "   border: 2px solid #FFD700; "
        "   color: #FFD700; "
        "}"
        ".btn-secondary:hover { "
        "   background-color: rgba(255, 215, 0, 0.1); "
        "}"
        ".btn-secondary:active { "
        "   background-color: rgba(255, 215, 0, 0.2); "
        "}"

        // Tertiary Action (Watch Replay) - Subtler Button
        ".btn-tertiary { "
        "   background-color: rgba(255, 215, 0, 0.1); "
        "   color: #FFD700; "
        "   border: 1px solid #FFD700; "
        "   border-radius: 20px; "
        "   padding: 8px 16px; "
        "}"
        ".btn-tertiary:hover { "
        "   background-color: rgba(255, 215, 0, 0.2); "
        "   color: #FFF; "
        "   border-color: #FFF; "
        "}"

        // Destructive / Leave - Red
        ".btn-danger { "
        "   background-color: #B71C1C; "
        "   color: #FFEBEE; "
        "   border: 1px solid #E57373; "
        "}"
        ".btn-danger:hover { "
        "   background-color: #C62828; "
        "}"

        // Special Game Actions
        ".btn-bid-g { " /* Inherit Primary but bigger */
        "   font-size: 24px; padding: 15px 30px; border-radius: 50px; "
        "   background-image: linear-gradient(to bottom, #FFD700, #F57F17); "
        "   color: #3E2723; "
        "   box-shadow: 0 6px 12px rgba(255, 215, 0, 0.3); "
        "}"
        ".btn-bid-g:hover { "
        "   background-image: linear-gradient(to bottom, #FFEA00, #F9A825); "
        "   box-shadow: 0 8px 16px rgba(255, 215, 0, 0.4); "
        "}"

        // --- INPUTS ---
        "entry { "
        "   background-color: #4E0404; "
        "   color: #FFECB3; "
        "   border: 2px solid #5D4037; "
        "   border-radius: 8px; "
        "   padding: 10px; "
        "   caret-color: #FFD700; "
        "}"
        "entry:focus { "
        "   border-color: #FFD700; "
        "   box-shadow: 0 0 4px rgba(255, 215, 0, 0.4); " /* Reduced Glow */
        "}"

        // --- CONTAINERS ---
        // Login
        ".login-box { background-color: #3E0404; border: 1px solid #FFD700; border-radius: 15px; padding: 40px; box-shadow: 0 10px 30px rgba(0,0,0,0.6); }"
        ".login-title { font-size: 32px; font-weight: 900; color: #FFD700; margin-bottom: 20px; text-shadow: 2px 2px 4px rgba(0,0,0,0.5); }"
        
        // Lobby
        ".lobby-box { background-color: #2B0505; padding: 20px; }"
        ".lobby-title { font-size: 22px; font-weight: 800; color: #FFD700; border-bottom: 2px solid #B71C1C; padding-bottom: 5px; margin-bottom: 10px; }"
        
        ".room-list { background-color: #1A0202; border: 1px solid #5D4037; border-radius: 8px; color: #FFF8E1; }"
        "treeview { background-color: #1A0202; color: #FFF8E1; border: none; }"
        "treeview:selected { background-color: #B71C1C; color: #FFD700; }"
        "treeview header button { background-color: #3E0404; color: #FFECB3; font-weight: bold; border: none; }"
        
        // Game
        ".game-header { background-color: #3E0404; border-bottom: 2px solid #FFD700; padding: 10px; }"
        ".game-image { background-color: #000; border: 4px solid #FFD700; border-radius: 10px; box-shadow: 0 0 15px rgba(255, 215, 0, 0.2); }"
        ".product-name { font-size: 24px; font-weight: 900; color: #FFD700; margin: 10px 0; text-shadow: 1px 1px 2px #000; }"
        ".input-bid { font-size: 28px; font-weight: bold; border-color: #FFD700; }"
        
        ".game-log { font-family: 'Monospace'; font-size: 13px; background-color: #1A0202; color: #FFECB3; padding: 10px; border-radius: 8px; border: 1px solid #5D4037; }"
        
        // User Badge
        ".user-badge { background-color: #4E0404; color: #FFD700; border: 1px solid #FFD700; border-radius: 15px; padding: 5px 15px; font-weight: bold; }"
        
        // Popups & Dialogs
        "dialog, messagedialog { background-color: #3E0404; color: #FFD700; }"
        "dialog label, messagedialog label { color: #FFF8E1; font-weight: bold; }"
        "dialog button { background-color: #FFD700; color: #B71C1C; border: none; font-weight: bold; }"
        "dialog button:hover { background-color: #FFEA00; }"
        
        ".toast-box { background-color: #4E0404; color: #FFD700; border: 2px solid #FFD700; border-radius: 10px; padding: 15px; box-shadow: 0 5px 15px rgba(0,0,0,0.5); }"
        
        // Dropdowns & Menus
        "menu { background-color: #3E0404; color: #FFD700; border: 1px solid #FFD700; }"
        "menuitem { padding: 5px; }"
        "menuitem:hover, menuitem:active { background-color: #FFD700; color: #B71C1C; }"
        "combobox { color: #FFD700; background-color: #4E0404; }";

    gtk_css_provider_load_from_data(provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void activate(GtkApplication *app, gpointer user_data) {
    load_css(); // Load styles
    window = gtk_application_window_new(app);
    // Custom Header Bar
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Price Is Right");
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    
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
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_login), "btn-primary");
    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_connect_click), NULL);
    
    GtkWidget *btn_reg = gtk_button_new_with_label("Create Account");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_reg), "btn-secondary");
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
    
    // Lobby Header
    GtkWidget *lobby_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *lbl_lobby = gtk_label_new("Available Rooms");
    gtk_widget_set_halign(lbl_lobby, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_lobby), "lobby-title");
    
    
    lbl_lobby_user = gtk_label_new("User: ???");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_lobby_user), "user-badge");
    
    gtk_box_pack_start(GTK_BOX(lobby_header), lbl_lobby, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(lobby_header), lbl_lobby_user, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_lobby), lobby_header, FALSE, FALSE, 0);
    
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

    // Empty State Label
    lbl_empty_rooms = gtk_label_new("No rooms found. Create one!");
    // Recycle lobby-title style or just use default text color? Lobby title has border-bottom, maybe overkill.
    // Let's manually style it or use a new class if needed. Using login-title for emphasis? Too big.
    // Let's assume default label inherits body text color (Milk White) which is good.
    // Make it italic for nuance?
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC));
    gtk_label_set_attributes(GTK_LABEL(lbl_empty_rooms), attrs);
    pango_attr_list_unref(attrs);
    
    gtk_box_pack_start(GTK_BOX(box_lobby), lbl_empty_rooms, FALSE, FALSE, 10);
    gtk_widget_set_visible(lbl_empty_rooms, FALSE);

    // Online Players List
    GtkWidget *lbl_onl = gtk_label_new("Online Players");
    gtk_widget_set_halign(lbl_onl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_onl), "lobby-title");
    gtk_box_pack_start(GTK_BOX(box_lobby), lbl_onl, FALSE, FALSE, 5);

    store_players_online = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    list_online_players = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store_players_online));
    gtk_style_context_add_class(gtk_widget_get_style_context(list_online_players), "room-list");
    
    GtkCellRenderer *r2 = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_online_players), -1, "Player", r2, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list_online_players), -1, "Status", r2, "text", 1, NULL);

    GtkWidget *scroll_onl = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll_onl, -1, 150);
    gtk_container_add(GTK_CONTAINER(scroll_onl), list_online_players);
    gtk_box_pack_start(GTK_BOX(box_lobby), scroll_onl, FALSE, FALSE, 0);
    
    GtkWidget *lobby_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    btn_create = gtk_button_new_with_label("+ Create Room");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_create), "btn-primary");
    g_signal_connect(btn_create, "clicked", G_CALLBACK(on_create_room_click), NULL);
    
    btn_join = gtk_button_new_with_label("Join Selected ->");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_join), "btn-secondary");
    g_signal_connect(btn_join, "clicked", G_CALLBACK(on_join_room_click), NULL);
    
    gtk_box_pack_start(GTK_BOX(lobby_actions), btn_create, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(lobby_actions), btn_join, FALSE, FALSE, 0);
    
    // Replay Button on SAME line
    btn_replay = gtk_button_new_with_label("Watch Replay"); /* Use Global */
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_replay), "btn-tertiary");
    gtk_box_pack_start(GTK_BOX(lobby_actions), btn_replay, FALSE, FALSE, 0);
    g_signal_connect(btn_replay, "clicked", G_CALLBACK(on_replay_click), NULL);
    
    gtk_box_pack_start(GTK_BOX(box_lobby), lobby_actions, FALSE, FALSE, 0);
    
    gtk_stack_add_named(GTK_STACK(stack), box_lobby, "lobby");
    
    // --- 3. Game Screen ---
    GtkWidget *box_game = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    // Header
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(head), "game-header");
    
    lbl_game_title = gtk_label_new("Game Room");
    lbl_game_user = gtk_label_new("User: ???");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_game_user), "user-badge");
    lbl_round = gtk_label_new("Round: -/-");
    lbl_score = gtk_label_new("Score: 0");
    lbl_timer = gtk_label_new("Time: --"); /* NEW */
    
    GtkWidget *btn_leave = gtk_button_new_with_label("Leave");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_leave), "btn-danger");
    g_signal_connect(btn_leave, "clicked", G_CALLBACK(on_leave_click), NULL);
    
    // Start Button (Hidden by default)
    btn_start_game = gtk_button_new_with_label("Start Game");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_start_game), "btn-primary");
    g_signal_connect(btn_start_game, "clicked", G_CALLBACK(on_start_click), NULL);
    gtk_widget_set_visible(btn_start_game, FALSE); 

    // Invite Button
    btn_invite = gtk_button_new_with_label("+ Invite");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_invite), "btn-secondary");
    g_signal_connect(btn_invite, "clicked", G_CALLBACK(on_invite_click), NULL);

    gtk_box_pack_start(GTK_BOX(head), lbl_game_title, FALSE, FALSE, 0);
    // gtk_box_pack_start(GTK_BOX(head), lbl_game_user, FALSE, FALSE, 10); /* Moved */
    gtk_box_pack_start(GTK_BOX(head), lbl_round, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(head), lbl_score, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(head), lbl_timer, TRUE, TRUE, 0); /* NEW */
    gtk_box_pack_end(GTK_BOX(head), btn_leave, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(head), btn_start_game, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(head), btn_invite, FALSE, FALSE, 5); /* NEW */
    gtk_box_pack_end(GTK_BOX(head), lbl_game_user, FALSE, FALSE, 10); /* NEW: Right Aligned */
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
