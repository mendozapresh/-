#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include "game_logic.h" // Needed for GameMessage struct

// --- Configuration ---
#define NET_PORT 8080
#define RETRY_DELAY_MS 500
#define MAX_RETRIES 3

// --- Lifecycle ---
bool net_init(int port);
void net_cleanup(void);

// --- Connection ---
void net_set_peer(const char *ip, int port);
bool net_is_peer_set(void);

// --- Sending (Reliable) ---
// Formats the key:value string, attaches seq number, and adds to retry queue
void net_send_game_message(const char *type, const char *extra_data);
// Sends a chat message (also reliable)
void net_send_chat(const char *sender, const char *text);

// --- Receiving ---
// Call this every frame. It handles ACKs, Retries, and returns true if a 
// new valid game message is ready in *out_msg.
bool net_process_updates(GameMessage *out_msg);

// --- Utils ---
int net_get_next_sequence(void);
const char* net_get_peer_ip(void);

// --- GLUE CODE PROTOTYPES (Required for Member 2's code) ---
void network_send_message(const char *payload);
int network_get_next_sequence(void);

#endif