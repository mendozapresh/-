#include "chat.h"
#include "network.h"  
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_value(char c) {
    const char *p = strchr(base64_table, c);
    return p ? (int)(p - base64_table) : -1;
}

static bool decode_base64(const char *in, const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;

    int val = 0, valb = -8;
    for (int i = 0; in[i] && in[i] != '='; i++) {
        int v = base64_value(in[i]);
        if (v < 0) continue;
        val = (val << 6) + v;
        valb += 6;
        if (valb >= 0) {
            fputc((val >> valb) & 0xFF, fp);
            valb -= 8;
        }
    }

    fclose(fp);
    return true;
}

void send_chat_text(const char *sender, const char *text)
{
    char payload[2048];
    snprintf(payload, sizeof(payload),
        "sender_name: %s\n"
        "content_type: TEXT\n"
        "message_text: %s\n",
        sender, text);

    net_send_game_message("CHAT_MESSAGE", payload);
}

void send_chat_sticker(const char *sender, const char *file_path)
{
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        printf("[CHAT] Sticker not found: %s\n", file_path);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    unsigned char *data = malloc(size);
    fread(data, 1, size, fp);
    fclose(fp);

    char *encoded = malloc(size * 2);
    int e = 0;
    for (long i = 0; i < size; i += 3) {
        int b1 = data[i];
        int b2 = (i+1<size) ? data[i+1] : 0;
        int b3 = (i+2<size) ? data[i+2] : 0;

        encoded[e++] = base64_table[b1>>2];
        encoded[e++] = base64_table[((b1&3)<<4)|(b2>>4)];
        encoded[e++] = (i+1<size) ? base64_table[((b2&15)<<2)|(b3>>6)] : '=';
        encoded[e++] = (i+2<size) ? base64_table[b3&63] : '=';
    }
    encoded[e] = 0;

    char payload[65536];
    snprintf(payload, sizeof(payload),
        "sender_name: %s\n"
        "content_type: STICKER\n"
        "sticker_data: %s\n",
        sender, encoded);

    net_send_game_message("CHAT_MESSAGE", payload);

    free(data);
    free(encoded);
}

bool parse_chat_message(const char *raw, ChatMessage *out)
{
    memset(out, 0, sizeof(*out));

    if (!strstr(raw, "message_type: CHAT_MESSAGE"))
        return false;

    char linebuf[60000];
    strcpy(linebuf, raw);

    char *line = strtok(linebuf, "\n");
    while (line) {
        if (strncmp(line, "sender_name:", 12) == 0)
            sscanf(line+12, "%63s", out->sender_name);

        else if (strncmp(line, "content_type:", 13) == 0) {
            char ct[16];
            sscanf(line+13, "%15s", ct);
            if (strcmp(ct, "TEXT") == 0) out->content_type = CHAT_TEXT;
            else out->content_type = CHAT_STICKER;
        }

        else if (strncmp(line, "message_text:", 13) == 0)
            strncpy(out->message_text, line+13, sizeof(out->message_text));

        else if (strncmp(line, "sticker_data:", 13) == 0) {
            char *b64 = line + 13;
            snprintf(out->sticker_filename, sizeof(out->sticker_filename),
                     "received_sticker_%d.png", out->sequence_number);
            decode_base64(b64, out->sticker_filename);
        }

        else if (strncmp(line, "sequence_number:", 16) == 0)
            out->sequence_number = atoi(line+16);

        line = strtok(NULL, "\n");
    }

    return true;
}

void display_chat_message(const ChatMessage *msg)
{
    if (msg->content_type == CHAT_TEXT) {
        printf("[CHAT] %s: %s\n", msg->sender_name, msg->message_text);
    } else {
        printf("[CHAT] %s sent a sticker → %s\n",
            msg->sender_name, msg->sticker_filename);
    }
}
