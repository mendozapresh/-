#ifndef CHAT_H
#define CHAT_H

#include <stdbool.h>

typedef enum {
    CHAT_TEXT,
    CHAT_STICKER
} ChatContentType;

typedef struct {
    char sender_name[64];
    ChatContentType content_type;
    char message_text[512];
    char sticker_filename[128];
    int sequence_number;
} ChatMessage;

void send_chat_text(const char *sender, const char *text);
void send_chat_sticker(const char *sender, const char *file_path);

bool parse_chat_message(const char *raw, ChatMessage *out);
void display_chat_message(const ChatMessage *msg);

#endif
