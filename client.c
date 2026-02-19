#include <stdio.h>
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define LENGTH 50
#define SERVER_PORT 8909
#define USERNAME_SIZE 64
#define MSG_TYPE_CHAT 1
#define MSG_TYPE_DISCONNECT 2
#define MAX_ROOMS 5
#define MSG_SIZE 1024

typedef struct {
    int id;      // 1–5
} ChatRoom;

ChatRoom rooms[MAX_ROOMS];
volatile int room_count = 0;

typedef struct {
    int msg_type;
    int payload_len;
    int checksum;
}MESSAGE_HEADER;

typedef struct {
    int sock_fd;
    WINDOW *win2;
    int height;
    int *cursor;
    int *running;
}Receiver;

//int room=0;
//pthread_mutex_t win2_mutex = PTHREAD_MUTEX_INITIALIZER;

int parse_rooms_response(const char *msg)
{
    room_count = 0;

    if (strncmp(msg, "ROOMS", 5) != 0)
        return 0;

    char buffer[MSG_SIZE];
    strncpy(buffer, msg, MSG_SIZE);

    char *word = strtok(buffer, " "); // "ROOMS"
    word = strtok(NULL, " ");// nr de camere

    if (word==NULL)
        return 0;

    int count = strtol(word, NULL, 10);

    for (int i = 0; i < count && i < MAX_ROOMS; i++)
    {
        word = strtok(NULL, " ");
        if (!word)
            break;

        rooms[room_count].id = strtol(word, NULL, 10);
        room_count++;
    }

    /*printf("Rooms active: %d\n", room_count);
    for (int i = 0; i < room_count; i++)
        printf("Room ID: %d\n", rooms[i].id);*/


    return 1; // mesaj parsat
}


int calculate_checksum(const char *payload, int len)
{
    int sum = 0;
    for(int i = 0; i < len; i++)
        sum ^= payload[i];
    return sum;
}

//pentru noile comenzi /join, /create, /exit
void send_command(int sockfd, const char *cmd)
{
    MESSAGE_HEADER h;
    h.msg_type = MSG_TYPE_CHAT;
    h.payload_len = strlen(cmd);
    h.checksum = calculate_checksum(cmd, h.payload_len);

    write(sockfd, &h, sizeof(h));
    write(sockfd, cmd, h.payload_len);
}


void *recieve_thread(void *arg) {
    Receiver *receiver = (Receiver *)arg;
    int sockfd = receiver->sock_fd;
    WINDOW *win2 = receiver->win2;
    int height = receiver->height;
    int *cursor = receiver->cursor;
    int *running = receiver->running;

    MESSAGE_HEADER message;
    char *payload=NULL;

    while (*running) {
        int bytes=read(sockfd, &message, sizeof(MESSAGE_HEADER)); //citeste header-ul mesajului
        if (bytes <=0) {
            break;
        }
        if (message.payload_len > 0) {
            payload=(char *)malloc(message.payload_len + 1);
            int total=0;
            while (total < message.payload_len) {
                //citim pana cand avem payload_len octeti
                int x=read(sockfd, payload + total, message.payload_len - total); 
                if (x<=0) {
                    break;
                }
                total += x;
            }
            /*payload[message.payload_len] = '\0';
            if (strstr(payload, "You joined Room") != NULL ||
                    strstr(payload, "Room created") != NULL) {
                room = 1;
    } debugged for 2 hours...*/

            if (parse_rooms_response(payload))
            {
                // e mesaj de tip ROOMS care nu se afiseaza (server afisa intr-un chat inainte)
                free(payload);
                continue;
            }

            mvwprintw(win2, (*cursor)++, 2, "%s", payload);
            if (*cursor >= height - 1) {
                scroll(win2);
                *cursor = height - 2;
            }
            wrefresh(win2);

            free(payload);
        }
    }
    return NULL;
}

void show_error(const char *msg, int login_x, int login_y, int login_height, int login_width) {
    //pentru error la nume si la numar maxim de chaturi
    WINDOW *error_win = newwin(login_height, login_width, login_x + login_height, login_y);
    box(error_win, 0, 0);
    mvwprintw(error_win, 1, 2, "%s", msg);
    wrefresh(error_win);
    napms(1500);
    delwin(error_win);
}

void open_chat_window(const char *user, const char *chat_name, int sockfd) {
    //room=0;
    clear();
    refresh();

    int height = 20, width = 70, x = 5, y = 5;
    int min_height = 3;

    int cursor=1;
    int running = 1;

    //WINDOW *win1 = newwin(min_height, width, x, y);
    WINDOW *win2 = newwin(height, width, x + min_height, y);
    WINDOW *win3 = newwin(min_height, width, x + min_height + height, y);

    //scrollok(win1, TRUE);
    //scrollok(win2, TRUE);
    //scrollok(win3, TRUE);

    //box(win1, 0, 0);
    //wprintw(win1, "Users");

    box(win2, 0, 0);
    wprintw(win2, chat_name);

    box(win3, 0, 0);
    wprintw(win3, "Message");

    //wrefresh(win1);
    wrefresh(win2);
    wrefresh(win3);

    //wmove(win1, 1, 2);
    //wprintw(win1, "%s", user);
    //wrefresh(win1);

    char input[LENGTH];
    MESSAGE_HEADER message;

    Receiver args;
    args.sock_fd = sockfd;
    args.win2 = win2;
    args.height = height;
    args.cursor = &cursor;
    args.running = &running;
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, recieve_thread, &args);

    while (1) {

        wmove(win3, 1, 2);
        wclrtoeol(win3);
        wrefresh(win3);

        wgetnstr(win3, input, LENGTH);
        input[strcspn(input, "\r\n")] = '\0';
        if (strcmp(input, "exit") == 0) {
            //inlocuieste asta cu un buton de exit
            const char* cmd = "/exit";
            send_command(sockfd, cmd);
            running=0;
            //room=0;
            break; //iesi din chat
        }
        else {
            message.msg_type = MSG_TYPE_CHAT;
            message.payload_len = strlen(input);
            message.checksum = calculate_checksum(input, message.payload_len);

            //trimite header si payload
            write(sockfd, &message, sizeof(MESSAGE_HEADER));
            write(sockfd, input, message.payload_len);
            //afiseaza local
            //din server nu se trimite propriul mesaj for some reason

            mvwprintw(win2, cursor++, 2, "%s: %s", user, input);
            if (cursor >= height - 1) {
                scroll(win2);
                cursor = height - 2;
            }
            wrefresh(win2);

        }


        wrefresh(win2);
    }

    pthread_join(recv_thread, NULL);
    //delwin(win1);
    delwin(win2);
    delwin(win3);

    //clear();
    //refresh();
}


int pick_free_room_id(void) {
    int used[MAX_ROOMS + 1] = {0}; // 1..5
    //nu se mai face permutare dupa ce un chat e sters
    //cautam id-urile si vedem care e liber
    for (int i = 0; i < room_count; i++) {
        if (rooms[i].id >= 1 && rooms[i].id <= MAX_ROOMS)
            used[rooms[i].id] = 1;
    }
    for (int id = 1; id <= MAX_ROOMS; id++)
        if (!used[id]) return id;
    return -1;
}


void draw_rooms(WINDOW *room_win[MAX_ROOMS], WINDOW *join_win[MAX_ROOMS], int room_y[MAX_ROOMS])
{
    int start_y = 10;

    for (int i = 0; i < room_count; i++)
    {
        room_y[i] = start_y + i * 4;

        room_win[i] = newwin(3, 50, room_y[i], 10);
        box(room_win[i], 0, 0);
        mvwprintw(room_win[i], 1, 2, "Room %d", rooms[i].id);
        wrefresh(room_win[i]);

        join_win[i] = newwin(3, 10, room_y[i], 65);
        box(join_win[i], 0, 0);
        mvwprintw(join_win[i], 1, 2, "Join");
        wrefresh(join_win[i]);
    }
}

void clear_rooms(WINDOW *room_win[MAX_ROOMS], WINDOW *join_win[MAX_ROOMS])
{
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (room_win[i]) {
            delwin(room_win[i]);
            room_win[i] = NULL;
        }
        if (join_win[i]) {
            delwin(join_win[i]);
            join_win[i] = NULL;
        }
    }
}

void draw_window(WINDOW *room_win[MAX_ROOMS], WINDOW *join_win[MAX_ROOMS], int room_y[MAX_ROOMS], WINDOW* button1, WINDOW* button2, WINDOW* button3, WINDOW* button4, int sockfd) {
    box(button1, 0, 0);
    mvwprintw(button1, 1, 3, "Create Chat Room");
    wrefresh(button1);

    box(button2, 0, 0);
    mvwprintw(button2, 1, 3, "Exit");
    wrefresh(button2);

    box(button3, 0, 0);
    mvwprintw(button3, 1, 3, "Refresh");
    wrefresh(button3);

    box(button4, 0, 0);
    mvwprintw(button4, 1, 3, "Users");
    wrefresh(button4);

    clear_rooms(room_win, join_win);
    send_command(sockfd, "/list_rooms");


    MESSAGE_HEADER h;

    read(sockfd, &h, sizeof(h));
    char buf[h.payload_len + 1];
    read(sockfd, buf, h.payload_len);
    buf[h.payload_len] = '\0';
    parse_rooms_response(buf);
    draw_rooms(room_win, join_win, room_y);
}

void open_users_window(int sockfd) {
    WINDOW *users_win=newwin(20, 70, 10, 10);
    box(users_win, 0, 0);

    wrefresh(users_win);
    send_command(sockfd, "/list");
    MESSAGE_HEADER h;

    read(sockfd, &h, sizeof(h));
    char buf[h.payload_len + 1];
    read(sockfd, buf, h.payload_len);
    buf[h.payload_len] = '\0';
    mvwprintw(users_win, 1, 2, " %s", buf);
    wrefresh(users_win);
    int ex=60, ey=6;
    int eh = 3, ew = 15;
    WINDOW *button = newwin(eh, ew, ey, ex);
    box(button, 0, 0);
    mvwprintw(button, 1, 3, "Exit");
    wrefresh(button);
    MEVENT event;
    while (1) {
        int ch = getch();
        if (ch == KEY_MOUSE) {
            if (getmouse(&event) == OK)
            {
                if (event.y >= ey && event.y < ey + eh &&
                    event.x >= ex && event.x < ex + ew && (event.bstate & BUTTON1_CLICKED)) {
                const char* cmd = "/exit";
                //send_command(sockfd, cmd);
                break;
                    }
            }
        }

    }

    delwin(users_win);
    delwin(button);
    clear();
    refresh();

}


int main(int argc, char** argv) {
    //call with:  gcc client.c -o client -lncurses -lpthread

    initscr();
    cbreak();
    int login_height = 3, login_width = 50, login_x = 10, login_y = 10;
    WINDOW *login_win = newwin(login_height, login_width, login_x, login_y);
    box(login_win, 0, 0);
    wprintw(login_win, "Login");
    wrefresh(login_win);

    char user[LENGTH];

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        endwin();
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        endwin();
        perror("connect");
        exit(EXIT_FAILURE);
    }


    while (1) {
        wmove(login_win, 1, 2);
        wclrtoeol(login_win);
        wrefresh(login_win);

        wgetnstr(login_win, user, LENGTH);

        if (strcmp(user, "") == 0) {
            show_error("Enter a valid username", login_x, login_y, login_height, login_width);
            continue;
        }

        //trimitem usernameul
        if (write(sockfd, user, strlen(user)) <= 0) {
            show_error("Connection error", login_x, login_y, login_height, login_width);
            break;
        }

        char response[100];
        int n = read(sockfd, response, sizeof(response)-1);

        if (n <= 0) {
            // daca serverul a inchis conexiunea
            show_error("Server disconnected", login_x, login_y, login_height, login_width);
            //reconectare
            close(sockfd);

            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) {
                endwin();
                perror("socket");
                exit(EXIT_FAILURE);
            }

            if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                endwin();
                perror("connect");
                exit(EXIT_FAILURE);
            }

            continue;
        }

        response[n] = '\0';

        if (strcmp(response, "USERNAME_OK\n") == 0) {
            break; //nume acceptat
        } else {
            show_error("Username already exists", login_x, login_y, login_height, login_width);

            close(sockfd);

            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) {
                endwin();
                perror("socket");
                exit(EXIT_FAILURE);
            }

            if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                endwin();
                perror("connect");
                exit(EXIT_FAILURE);
            }

            //cerem alt nume
        }
    }


    //dupa login ne trebuie chatroom urile active

    delwin(login_win);
    clear();
    refresh();

    //  enter
    getch();

    clear();
    refresh();

    //noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    mousemask(ALL_MOUSE_EVENTS, NULL);




    int by = 6, bx = 10;
    int bh = 3, bw = 25;

    int ex=60, ey=6;
    int eh = 3, ew = 15;

    int rx=40, ry=6;
    int rh=3, rw=15;

    WINDOW *button1 = newwin(bh, bw, by, bx);
    box(button1, 0, 0);
    mvwprintw(button1, 1, 3, "Create Chat Room");
    wrefresh(button1);

    WINDOW *button2 = newwin(eh, ew, ey, ex);
    box(button2, 0, 0);
    mvwprintw(button2, 1, 3, "Exit");
    wrefresh(button2);

    WINDOW *button3 = newwin(rh, rw, ry, rx);
    box(button3, 0, 0);
    mvwprintw(button3, 1, 3, "Refresh");
    wrefresh(button3);

    int ux=80, uy=6;
    int uh=3, uw=15;
    WINDOW *button4 = newwin(uh, uw, uy, ux);
    box(button4, 0, 0);
    mvwprintw(button4, 1, 3, "Users");
    wrefresh(button4);

    //Buton de refresh: se apeleaza functia de redesenare a butoanelor si camerelor existente
    //Actualiarea design-ului pentru client in cazul camerelor nou create sau sterse de alti clienti



    WINDOW *room_win[MAX_ROOMS] = {0};
    WINDOW *join_win[MAX_ROOMS] = {0};
    int room_y[MAX_ROOMS];

    send_command(sockfd, "/list_rooms");
    MESSAGE_HEADER h;
    read(sockfd, &h, sizeof(h));
    char buf[h.payload_len + 1];
    read(sockfd, buf, h.payload_len);
    buf[h.payload_len] = '\0';
    parse_rooms_response(buf);
    draw_rooms(room_win, join_win, room_y);

    MEVENT event;

    while (1)
    {
        int ch = getch();

        if (ch == KEY_MOUSE)
        {
            if (getmouse(&event) == OK)
            {
                if (event.y >= by && event.y < by + bh &&
                        event.x >= bx && event.x < bx + bw && (event.bstate & BUTTON1_CLICKED))
                {
                    //butonul Create Rooms apasat
                    int id = pick_free_room_id();
                    if (id == -1) {
                        // in cazul in care sunt deja 5 chat-uri existente afișează un mesaj gen "No free rooms"
                        int rooms_error_height = 3, rooms_error_width = 50, rooms_error_x = 0, rooms_error_y = 10;
                        show_error("No free rooms available!", rooms_error_x, rooms_error_y, rooms_error_height, rooms_error_width);
                    } else {
                        char cmd[64];
                        snprintf(cmd, sizeof(cmd), "/create %d %s", id, user);
                        send_command(sockfd, cmd);
                        open_chat_window(user, "Chat Room", sockfd);
                    }

                    clear();
                    refresh();

                    draw_window(room_win, join_win, room_y, button1, button2, button3, button4, sockfd);

                    continue;
                }

                for (int i = 0; i < room_count; i++)
                {



                    if (event.y >= room_y[i] && event.y < room_y[i] + 3 &&
                        event.x >= 65 && event.x < 75 && (event.bstate & BUTTON1_CLICKED))
                    {
                        //Unul din butoanele de join apasate
                        char cmd[32];
                        snprintf(cmd, sizeof(cmd), "/join %d", rooms[i].id);
                        send_command(sockfd, cmd);

                        open_chat_window(user, "Chat Room", sockfd);

                        clear();
                        refresh();
                        draw_window(room_win, join_win, room_y, button1, button2, button3, button4, sockfd);
                    }
                }


                if (event.y >= ey && event.y < ey + eh &&
                    event.x >= ex && event.x < ex + ew && (event.bstate & BUTTON1_CLICKED))
                {
                    //Butonul de exit: aici se iese din program
                    endwin();
                    close(sockfd);
                    return 0;
                }

                if (event.y >= ry && event.y < ry + rh && event.x >= rx && event.x < rx + rw && (event.bstate & BUTTON1_CLICKED)) {
                    //Butonul de refresh
                    clear();
                    refresh();
                    draw_window(room_win, join_win, room_y, button1, button2, button3,button4, sockfd);
                }
                if (event.y >= uy && event.y < uy + uh && event.x >= ux && event.x < ux + uw && (event.bstate & BUTTON1_CLICKED)) {
                    //Butonul de users
                    clear();
                    refresh();
                    open_users_window(sockfd);
                    clear();
                    refresh();
                    draw_window(room_win, join_win, room_y, button1, button2, button3,button4, sockfd);
                }
            }
        }
    }

    return 0;
}
