#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <fcntl.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

// --- Internal State ---
static int sockfd = -1;
static struct sockaddr_in peer_addr;
static bool peer_known = false;
static int local_seq = 0;
static int remote_seq = 0;

// Reliability Buffer
typedef struct {
    bool active;
    char payload[4096];
    int seq;
    int retries;
    long long last_sent;
} PendingPacket;

static PendingPacket outgoing = {0};

// --- Time Helper ---
long long current_time_ms() {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000LL) + (tv.tv_usec / 1000);
#endif
}

// --- Initialization ---
bool net_init(int port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return false;

    // Non-blocking mode
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sockfd, FIONBIO, &mode);
#else
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in my_addr = {0};
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
        perror("Bind failed");
        return false;
    }
    printf("[NET] Listening on port %d\n", port);
    return true;
}

void net_cleanup() {
    if (sockfd >= 0) closesocket(sockfd);
#ifdef _WIN32
    WSACleanup();
#endif
}

void net_set_peer(const char *ip, int port) {
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &peer_addr.sin_addr);
    peer_known = true;
}

bool net_is_peer_set() { return peer_known; }
int net_get_next_sequence() { return local_seq + 1; }

// --- Raw Sending ---
void send_raw(const char *data) {
    if (!peer_known) return;
    sendto(sockfd, data, strlen(data), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
}

// --- Parsing Helper ---
void parse_kv(char *buffer, GameMessage *msg) {

    strncpy(msg->raw_buffer, buffer, 4095);
    msg->raw_buffer[4095] = '\0';
    
    char *line = strtok(buffer, "\n");
    while (line) {
        char *sep = strchr(line, ':');
        if (sep) {
            *sep = 0;
            char *val = sep + 1;
            while (*val == ' ') val++; // trim space

            if (strcmp(line, "message_type") == 0) strncpy(msg->message_type, val, 31);
            else if (strcmp(line, "move_name") == 0) strncpy(msg->move_name, val, 31);
            else if (strcmp(line, "attacker") == 0) strncpy(msg->attacker, val, 31); // Added to struct
            else if (strcmp(line, "winner") == 0) strncpy(msg->winner, val, 31);
            else if (strcmp(line, "damage_dealt") == 0) msg->damage_dealt = atoi(val);
            else if (strcmp(line, "defender_hp_remaining") == 0) msg->defender_hp_remaining = atoi(val);
            else if (strcmp(line, "seed") == 0) set_shared_rng_seed(atoi(val));
        }
        line = strtok(NULL, "\n");
    }
}

// --- Public Sending ---
void net_send_game_message(const char *type, const char *extra_data) {
    local_seq++;
    char buffer[4096];
    // Construct RFC compliant message
    int len = snprintf(buffer, sizeof(buffer), 
        "message_type: %s\n"
        "sequence_number: %d\n"
        "%s", type, local_seq, extra_data ? extra_data : "");

    // Store for reliability
    outgoing.active = true;
    outgoing.seq = local_seq;
    outgoing.retries = 0;
    outgoing.last_sent = current_time_ms();
    strncpy(outgoing.payload, buffer, sizeof(outgoing.payload));

    send_raw(buffer);
    printf("[NET] Sent Seq %d: %s\n", local_seq, type);
}

void net_send_chat(const char *sender, const char *text) {
    // Chat doesn't strictly need reliability in this simple version, 
    // but we wrap it to match the prompt's requirement for reliability.
    char extra[1024];
    snprintf(extra, sizeof(extra), "sender_name: %s\ncontent_type: TEXT\nmessage_text: %s\n", sender, text);
    net_send_game_message("CHAT_MESSAGE", extra);
}

// --- Processing Loop ---
bool net_process_updates(GameMessage *out_msg) {
    // 1. Handle Retries
    if (outgoing.active) {
        if (current_time_ms() - outgoing.last_sent > RETRY_DELAY_MS) {
            if (outgoing.retries < MAX_RETRIES) {
                printf("[NET] Timeout. Retrying Seq %d (%d/%d)\n", outgoing.seq, outgoing.retries+1, MAX_RETRIES);
                send_raw(outgoing.payload);
                outgoing.retries++;
                outgoing.last_sent = current_time_ms();
            } else {
                printf("[NET] Connection Lost (Max Retries).\n");
                outgoing.active = false; 
                // In real app, trigger game over here
            }
        }
    }

    // 2. Receive
    char buf[4096];
    struct sockaddr_in sender;
    socklen_t slen = sizeof(sender);
    int len = recvfrom(sockfd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&sender, &slen);

    if (len > 0) {
        buf[len] = 0;
        
        // Auto-detect peer if Joiner talks to Host
        if (!peer_known) {
            peer_addr = sender;
            peer_known = true;
            printf("[NET] Peer connected from %s\n", inet_ntoa(sender.sin_addr));
        }

        // Extract Headers
        int seq = -1, ack = -1;
        char *p = strstr(buf, "sequence_number: ");
        if (p) seq = atoi(p + 17);
        p = strstr(buf, "ack_number: ");
        if (p) ack = atoi(p + 12);

        // Handle ACK
        if (ack != -1) {
            if (outgoing.active && outgoing.seq == ack) {
                // printf("[NET] ACK Received for %d\n", ack);
                outgoing.active = false;
            }
            return false; // ACKs are internal, don't pass to game logic
        }

        // Handle Incoming Message
        if (seq != -1) {
            // Send ACK immediately
            char ack_pkt[64];
            snprintf(ack_pkt, sizeof(ack_pkt), "message_type: ACK\nack_number: %d\n", seq);
            send_raw(ack_pkt);

            // Deduplicate
            if (seq <= remote_seq && remote_seq != 0) {
                return false; // Duplicate
            }
            remote_seq = seq;

            // Parse for Game Logic
            memset(out_msg, 0, sizeof(GameMessage));
            parse_kv(buf, out_msg);
            return true;
        }
    }
    return false;
}

// --- GLUE CODE FOR MEMBER 2 COMPATIBILITY ---
void network_send_message(const char *payload) {
    // Member 2 constructs the full "key: val\n..." string.
    // My net_send_game_message expects type and extra_data separately.
    // We will parse the type out to use my reliability layer.
    
    char type[64] = {0};
    char extra[4096] = {0};
    
    const char *type_prefix = "message_type: ";
    char *p = strstr(payload, type_prefix);
    
    if (p) {
        p += strlen(type_prefix);
        char *end = strchr(p, '\n');
        if (end) {
            int len = end - p;
            if (len > 63) len = 63;
            strncpy(type, p, len);
            
            // Copy the rest as extra data, skipping the type line
            strcpy(extra, end + 1); 
        }
    }

    // Call my reliable sender
    // Note: Member 2 adds "sequence_number" manually. 
    // My layer also adds it. This might result in double headers, 
    // but the parser ignores duplicates. ideally, remove it from game_logic.c
    net_send_game_message(type, extra); 
}

int network_get_next_sequence() {
    return net_get_next_sequence();
}
