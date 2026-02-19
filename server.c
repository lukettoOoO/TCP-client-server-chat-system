#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>

#define MAX_CLIENTS 100
#define USERNAME_SIZE 64
#define SERVER_PORT 8909
#define MSG_SIZE 1024
#define MAX_ROOMS 5
#define NO_ROOM -1 //valoare pentru cand client-ul nu se afla in vreo camera

#define MSG_TYPE_CHAT 1
#define MSG_TYPE_DISCONNECT 2

//Mesajele au protocol propriu header si payload:
typedef struct {
    int msg_type;
    int payload_len; //nr octeti payload
    int checksum;
}MESSAGE_HEADER;

typedef struct {
    int fd; //socket client
    char name[USERNAME_SIZE];
    int current_room; //-1 daca nu se afla in vreo camera, 0-4 pentru camere
}Client;

#define MAX_ROOMS 5

typedef struct {
    int id;      // 1–5 (user-friendly)
} ChatRoom;

ChatRoom rooms[MAX_ROOMS];
int room_count = 0;


pthread_mutex_t user_list_mutex; //lista de clienti
Client connected_clients[MAX_CLIENTS];
int user_count = 0;

int calculate_checksum(const char *payload, int len)
{
    int sum = 0;
    for(int i = 0; i < len; i++)
        sum ^= payload[i];
    return sum;
}

//functie pentru a trimite un mesaj doar unui singur client
void send_response_to_client(int clientfd, const char* message_text)
{
    MESSAGE_HEADER header;
    header.msg_type = MSG_TYPE_CHAT;
    header.payload_len = strlen(message_text);
    header.checksum = calculate_checksum(message_text, header.payload_len);

    write(clientfd, &header, sizeof(MESSAGE_HEADER));
    write(clientfd, message_text, header.payload_len);
}

void remove_user(const char* username)
{
    pthread_mutex_lock(&user_list_mutex);

    for (int i = 0; i < user_count; i++) {
        if (strcmp(connected_clients[i].name, username) == 0)
        {
            //reactualizare lista de clienti
            for (int j = i; j < user_count - 1; j++)
            {
                connected_clients[j] = connected_clients[j + 1];
            }
            user_count--;
            printf("user %s removed from list; active users: %d\n", username, user_count);
            break;
        }
    }

    pthread_mutex_unlock(&user_list_mutex);
}

//functie de rutare a mesajului
void handle_message(int sender_fd, const char* buffer, int len)
{
    pthread_mutex_lock(&user_list_mutex);

    //identificam cine trimite mesajul si in ce camera e
    int sender_room = NO_ROOM;
    char sender_name[USERNAME_SIZE];
    for(int i = 0; i < user_count; i++)
    {
        if(connected_clients[i].fd == sender_fd)
        {
            sender_room = connected_clients[i].current_room;
            snprintf(sender_name, USERNAME_SIZE, "%s", connected_clients[i].name);
            break;
        }
    }

    //venit Bia, nu vreau mesaje pentru useri in no room
    if (sender_room == NO_ROOM) {
        pthread_mutex_unlock(&user_list_mutex);
        return;
    }
    //terminat
    MESSAGE_HEADER header;
    header.msg_type = MSG_TYPE_CHAT;
    header.payload_len = len;
    header.checksum = calculate_checksum(buffer, len);
    for(int i = 0; i < user_count; i++)
    {
        //Trimite mesajul doar catre userii care sunt in aceeasi camera cu sender-ul
        if(connected_clients[i].fd != sender_fd && connected_clients[i].current_room == sender_room)
        {
            write(connected_clients[i].fd, &header, sizeof(MESSAGE_HEADER));
            write(connected_clients[i].fd, buffer, len);
        }
    }

    pthread_mutex_unlock(&user_list_mutex);
}

void handle_disconnect(const char* username)
{
    //logica de anuntare a deconectarii trebuie sa tina cont de camera
    pthread_mutex_lock(&user_list_mutex);

    int user_room = NO_ROOM;
    //aflam camera in care se afla user-ul
    for(int i = 0; i < user_count; i++)
    {
        if(strcmp(connected_clients[i].name, username) == 0)
        {
            user_room = connected_clients[i].current_room;
            break;
        }
    }

    char disconnect_msg[MSG_SIZE];
    snprintf(disconnect_msg, MSG_SIZE, "%s has left the chat", username);

    MESSAGE_HEADER header;
    header.msg_type = MSG_TYPE_CHAT; //tot tip mesaj
    header.payload_len = strlen(disconnect_msg);
    header.checksum = calculate_checksum(disconnect_msg, header.payload_len);

    for(int i = 0; i < user_count; i++)
    {
        //anuntam doar ceilalti useri ce sunt in camera
        if(connected_clients[i].current_room == user_room)
        {
            write(connected_clients[i].fd, &header, sizeof(MESSAGE_HEADER));
            write(connected_clients[i].fd, disconnect_msg, header.payload_len);
        }
    }

    pthread_mutex_unlock(&user_list_mutex);
}

//functie de procesare a comenzilor
/*
 comenzi posibile:
 /list - afiseaza lista de utilizatori conectati
 /create <room_id> <user1> <user2> ... - creeaza o camera privata
 /join <id> - join intr-un chatroom
 /exit - iesire dintr-un chatroom
 */
//returneaza 1 daca a fost comanda, 0 daca e mesaj simplu
int process_command(int clientfd, char* buffer)
{
    buffer[strcspn(buffer, "\r\n")] = 0;

    // /list - afiseaza lista de utilizatori conectati
    if (strcmp(buffer, "/list") == 0)
    {
        char list_msg[MSG_SIZE] = "Online users:\n";
        pthread_mutex_lock(&user_list_mutex);
        for(int i = 0; i < user_count; i++) {
            strncat(list_msg, connected_clients[i].name, MSG_SIZE - strlen(list_msg) - 1);
            //adaugam in ce chatroom se afla
            char room_info[30];
            if(connected_clients[i].current_room == NO_ROOM)
                snprintf(room_info, 30, " (No chatroom)\n");
            else
                snprintf(room_info, 30, " (Room %d)\n", connected_clients[i].current_room + 1);

            strncat(list_msg, room_info, MSG_SIZE - strlen(list_msg) - 1);
        }
        pthread_mutex_unlock(&user_list_mutex);

        send_response_to_client(clientfd, list_msg);
        return 1;
    }

    //clientul trebuie sa afle de la server cate chatroom uri exista (from Bia)
    // /list_rooms - afiseaza chatroom-urile active
    if (strcmp(buffer, "/list_rooms") == 0)
    {
        int room_active[MAX_ROOMS] = {0};
        int count = 0;

        pthread_mutex_lock(&user_list_mutex);

        // marcam ce camere sunt folosite
        for (int i = 0; i < user_count; i++)
        {
            int r = connected_clients[i].current_room;
            if (r != NO_ROOM && r >= 0 && r < MAX_ROOMS)
                room_active[r] = 1;
        }

        char response[MSG_SIZE];
        strcpy(response, "ROOMS ");

        // numaram si listam camerele
        for (int i = 0; i < MAX_ROOMS; i++)
            if (room_active[i])
                count++;

        char tmp[16];
        snprintf(tmp, sizeof(tmp), "%d", count);
        strcat(response, tmp);

        for (int i = 0; i < MAX_ROOMS; i++)
        {
            if (room_active[i])
            {
                char room_id[8];
                snprintf(room_id, sizeof(room_id), " %d", i + 1); // 1-based
                strcat(response, room_id);
            }
        }


        pthread_mutex_unlock(&user_list_mutex);

        send_response_to_client(clientfd, response);
        return 1;
    }


    // /create <room_id>  - creeaza o camera privata
    if (strncmp(buffer, "/create", 7) == 0)
    {
        int room_id;
        char names[MSG_SIZE];

        // Extragem ID-ul camerei și restul string-ului cu nume
        if (sscanf(buffer + 8, "%d %[^\n]", &room_id, names) >= 2)
        {
            int internal_id = room_id - 1;
            if (internal_id >= 0 && internal_id < MAX_ROOMS)
            {
                pthread_mutex_lock(&user_list_mutex);
                //punem pe user-ul care a dat comanda in chatroom
                for(int i=0; i<user_count; i++)
                {
                    if(connected_clients[i].fd == clientfd)
                    {
                        connected_clients[i].current_room = internal_id;
                        break;
                    }
                }

                //punem pe ceilalti useri in chatroom
                char *name = strtok(names, " ");
                while (name != NULL)
                {
                    for (int i = 0; i < user_count; i++)
                    {
                        if (strcmp(connected_clients[i].name, name) == 0)
                        {
                            connected_clients[i].current_room = internal_id;
                            char notify[100];
                            snprintf(notify, 100, "[SERVER] You were moved to room %d by an admin.", room_id);
                            send_response_to_client(connected_clients[i].fd, notify);
                        }
                    }
                    name = strtok(NULL, " ");
                }
                pthread_mutex_unlock(&user_list_mutex);
                send_response_to_client(clientfd, "[SERVER] Room created and users invited.");
            }
        }
        return 1;
    }

    // /join <id> - join intr-un chatroom
    if (strncmp(buffer, "/join", 5) == 0)
    {
        int room_id = NO_ROOM;
        if (sscanf(buffer + 6, "%d", &room_id) == 1)
        {
            int internal_id = room_id - 1; //userul scrie 1-5 (ca sa fie mai user-friendly), la server se foloseste 0-4

            if (internal_id >= 0 && internal_id < MAX_ROOMS)
            {
                pthread_mutex_lock(&user_list_mutex);
                for(int i = 0; i < user_count; i++)
                {
                    if(connected_clients[i].fd == clientfd)
                    {
                        connected_clients[i].current_room = internal_id;
                        break;
                    }
                }
                pthread_mutex_unlock(&user_list_mutex);

                char msg[64];
                snprintf(msg, 64, "[SERVER] You joined Room %d", room_id);
                send_response_to_client(clientfd, msg);
            }
            else
            {
                send_response_to_client(clientfd, "[SERVER] Invalid room (1-5)!");
            }
        }
        return 1;
    }

    // /exit -iesire dintr-un chatroom
    if (strcmp(buffer, "/exit") == 0)
    {
        pthread_mutex_lock(&user_list_mutex);
        for(int i = 0; i < user_count; i++)
        {
            if(connected_clients[i].fd == clientfd)
            {
                connected_clients[i].current_room = NO_ROOM;
                break;
            }
        }
        pthread_mutex_unlock(&user_list_mutex);
        send_response_to_client(clientfd, "[SERVER] You are now in no chatroom.");
        return 1;
    }

    return 0; //nu a fost comanda
}

void* handle_client(void* arg)
{
    int clientfd = (intptr_t) arg;
    char name_buffer[USERNAME_SIZE];
    int bytes_read;
    int is_name_valid = 0;

    //first thing that client sends is name!!!
    if ((bytes_read = read(clientfd, name_buffer, sizeof(name_buffer) - 1)) > 0)
    {
        name_buffer[bytes_read] = '\0';
        name_buffer[strcspn(name_buffer, "\r\n")] = 0;
        printf("logging user: %s\n", name_buffer);

        //name validation
        pthread_mutex_lock(&user_list_mutex); //mai mult de un client nu poate accesa lista de useri
        int found = 0;
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(connected_clients[i].name, name_buffer) == 0)
            {
                found = 1;
                break;
            }
        }
        if (found)
            is_name_valid = 0;
        else
            is_name_valid = 1;

        if (is_name_valid && user_count < MAX_CLIENTS)
        {
            //Se adauga un nou client
            connected_clients[user_count].fd = clientfd;
            strncpy(connected_clients[user_count].name, name_buffer, USERNAME_SIZE);
            connected_clients[user_count].current_room = NO_ROOM;
            user_count++;
        }

        pthread_mutex_unlock(&user_list_mutex);

        //Trimite validare catre client
        if (is_name_valid)
        {
            write(clientfd, "USERNAME_OK\n", 12);
            printf("user %s successfully logged in; active users: %d\n", name_buffer, user_count);
        }
        else
        {
            write(clientfd, "USERNAME_ERROR\n", 15);
            printf("username %s already exists; closing connection\n", name_buffer);
        }
    }
    else
    {
        is_name_valid = 0;
    }

    ///message loop:
    if(is_name_valid)
    {
        MESSAGE_HEADER header;
        char *payload_buffer = NULL;
        int disconnect_requested = 0;
        while(1)
        {
            int header_bytes_read = read(clientfd, &header, sizeof(MESSAGE_HEADER));
            if(header_bytes_read <= 0)
                break; //Clientul a inchis conexiunea
            if(header_bytes_read != sizeof(MESSAGE_HEADER))
            {
                fprintf(stderr, "Incomplete header received from %s!\n", name_buffer);
                continue;
            }
            if(header.payload_len > 0)
            {
                payload_buffer = (char*)malloc((header.payload_len + 1) * sizeof(char));
                if(payload_buffer == NULL)
                {
                    perror("malloc payload_buffer");
                    break;
                }
                int total_payload_bytes_read = 0;
                while(total_payload_bytes_read < header.payload_len)
                {
                    int payload_bytes_read = read(clientfd, payload_buffer + total_payload_bytes_read, header.payload_len - total_payload_bytes_read);
                    if(payload_bytes_read <= 0)
                        break;
                    total_payload_bytes_read += payload_bytes_read;
                }
                payload_buffer[header.payload_len] = '\0';

                int calc_check = calculate_checksum(payload_buffer, header.payload_len);
                if(calc_check == header.checksum)
                {//Verificare checksum (type chat/disconnect/invalid)
                    if(header.msg_type == MSG_TYPE_CHAT)
                    {
                        //verifcare daca e comanda
                        char cmd_check[MSG_SIZE];
                        strncpy(cmd_check, payload_buffer, MSG_SIZE);
                        if(process_command(clientfd, cmd_check))
                        {
                            //daca a fost comanda, atunci nu avem niciun mesaj de trimis
                            //totul a fost procesat de functia process_command
                        }
                        else
                        {
                            char final_msg[MSG_SIZE];
                            snprintf(final_msg, MSG_SIZE, "%s: %s", name_buffer, payload_buffer);
                            printf("from %s: %s\n", name_buffer, payload_buffer);
                            handle_message(clientfd, final_msg, strlen(final_msg));
                        }
                    }
                    if(header.msg_type == MSG_TYPE_DISCONNECT)
                    {
                        printf("user %s requested disconnect\n", name_buffer);
                        disconnect_requested = 1;
                        handle_disconnect(name_buffer);
                        break;
                    }
                }
                else
                {
                    fprintf(stderr, "Invalid checksum from %s!\n", name_buffer);
                }
                free(payload_buffer);
            }
        }
        //client disconnected
        printf("user %s disconnected\n", name_buffer);
        remove_user(name_buffer);
    }
    close(clientfd);
    pthread_exit(NULL);
    return NULL;
}

void *accept_connections(void *arg)
{
    int sockfd = 0;
    int clientfd = 0;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    pthread_t thread;

    pthread_mutex_init(&user_list_mutex, NULL);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    ///server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if(bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if(listen(sockfd, 5) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("server listening on port: %d\n", SERVER_PORT);

    while(1)
    {
        client_len = sizeof(client_addr);
        clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        if(clientfd < 0)
        {
            perror("accept");
            continue;
        }
        printf("user connected\n");

        ///create thread for client
        if(pthread_create(&thread, NULL, handle_client, (void*)(intptr_t )clientfd) != 0)
        {
            perror("pthread_create");
            close(clientfd);
        }
        else
        {
            pthread_detach(thread);
        }
    }

    close(sockfd);
}

int main(void)
{
    pthread_t server_thread;
    printf("server starting...\n");

    int r = 0;
    if((r = pthread_create(&server_thread, NULL, accept_connections, NULL)) != 0)
    {
        fprintf(stderr, "phtread_create (server_thread): %s", strerror(r));
        exit(EXIT_FAILURE);
    }
    pthread_join(server_thread, NULL);

    pthread_mutex_destroy(&user_list_mutex);
    return 0;
}
