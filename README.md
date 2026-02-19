# TCP Chat System (Client-Server)

A real-time chat application implemented in C using TCP sockets and the POSIX threads (pthread) library. The project consists of a multi-threaded server for message routing and room management, and a client application featuring a text-based graphical user interface (GUI) built with **ncurses**.

## 👥 Team Members & Roles

- **Luca-Adrian Mihut**: Server Implementation
  - Designed the client-server communication logic.
  - Implemented thread-safe message routing and user management.
  - Developed the custom protocol for data transmission.
  
- **Bianca-Delia Grovu**: Client Implementation
  - Developed the ncurses-based GUI.
  - Implemented the client-side logic for connecting and interacting with the server.
  - Handled user input and real-time display updates.

## 🚀 Features

### Server Component
- **Concurrent Connections**: Handles multiple clients simultaneously using a multi-threaded architecture.
- **User Management**: 
  - Enforces unique usernames upon connection.
  - Maintains a real-time list of active users.
- **Message Routing**: Routes messages exclusively between users in the same chat room or globally if implemented.
- **Chat Room System**:
  - dynamic creation of private chat rooms.
  - Users can join (`/join`) and exit (`/exit`) rooms.
  - Supports up to 5 concurrent chat rooms.
- **Reliability**: Implements a custom checksum to ensure data integrity.

### Client Component
- **Interactive TUI**: A user-friendly text interface built with `ncurses`, supporting mouse interactions for buttons.
- **Real-time Updates**: Displays incoming messages and notifications immediately without blocking user input.
- **Room Management**:
  - **Create Room**: Invite specific users to a new private room.
  - **Join Room**: Browse and join active discussion rooms.
  - **User List**: View all currently connected users.
- **Visual Feedback**: dedicated windows for chat history, message input, and room lists.

<img width="882" height="562" alt="Captură de ecran din 2026-02-19 la 17 33 15" src="https://github.com/user-attachments/assets/c85dfb0b-d34a-45d1-8fd6-f4c5764269fa" />
<img width="890" height="521" alt="Captură de ecran din 2026-02-19 la 17 34 22" src="https://github.com/user-attachments/assets/98e39efc-2da1-4897-8b7e-80f3a0a7026c" />


## 🛠️ Technical Architecture

- **Language**: C
- **Communication Protocol**: TCP/IP Sockets
- **Concurrency**: POSIX Threads (`pthread`) with Mutex synchronization (`pthread_mutex`) for thread-safe access to shared resources.
- **Data Protocol**: Custom binary header structure ensures reliable message parsing:
  ```c
  typedef struct {
      int msg_type;      // Chat or Disconnect
      int payload_len;   // Length of the message
      int checksum;      // Integrity check
  } MESSAGE_HEADER;
  ```
- **GUI Library**: ncurses (for Client)

## 📋 Prerequisites

- **GCC Compiler**
- **Make** (optional)
- **Ncurses Library** (required for client)
  - *Ubuntu/Debian*: `sudo apt-get install libncurses5-dev libncursesw5-dev`
  - *macOS*: `brew install ncurses` (usually pre-installed)

## ⚙️ Compilation & Usage

### 1. Compile the Server
```bash
gcc server.c -o server -lpthread
```

### 2. Compile the Client
```bash
gcc client.c -o client -lncurses -lpthread
```

### 3. Run the System
First, start the server:
```bash
./server
```
The server will start listening on port **8909**.

Then, open one or more terminal windows and start the client(s):
```bash
./client
```

## 🎮 User Guide

1. **Login**: Enter a unique username when prompted.
2. **Main Dashboard**:
   - **Create Chat Room**: Creates a new room and allows you to invite other users.
   - **Join**: Select a room ID to join an existing conversation.
   - **Users**: See a list of all online users.
   - **Refresh**: Updates the list of available rooms.
   - **Exit**: Disconnects from the application.
3. **Chatting**: Once in a room, type your message and press Enter. Type `/exit` or click the exit button to leave the room.

## 📝 Commands
While the GUI buttons are the primary way to interact, the client supports these internal commands:
- `/list`: List connected users.
- `/list_rooms`: List active rooms.
- `/create <room_id> <user>`: Create a room.
- `/join <id>`: Join a specific room.
- `/exit`: Leave the current room.
