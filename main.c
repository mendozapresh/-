#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "network.h"
#include "game_logic.h"
#include "damage_calc.h"
#include "chat.h"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#define sleep_ms(x) Sleep(x)
int kbhit_check() { return _kbhit(); }
#else
#include <unistd.h>
#include <sys/select.h>
#define sleep_ms(x) usleep((x)*1000)
int kbhit_check() {
    struct timeval tv = {0L, 0L};
    fd_set fds; FD_ZERO(&fds); FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}
#endif

void print_prompt(BattleContext *ctx) {
    if (ctx->is_my_turn && ctx->state == STATE_WAITING_FOR_MOVE) {
        printf("\rAction> ");
        fflush(stdout);
    }
}

void print_battle_status(BattleContext *ctx) {
    printf("\n========================================\n");
    if (ctx->my_role == ROLE_SPECTATOR) {
        printf("      --- SPECTATOR MODE ---\n");
        printf("P1 (%s): %d HP  VS  P2 (%s): %d HP\n", 
            ctx->my_pokemon, ctx->my_hp, 
            ctx->opponent_pokemon, ctx->opponent_hp); // Spectator tracks both as "my" and "opponent" generic slots
    } else {
        printf("ME (%s): %d HP  VS  OPPONENT (%s): %d HP\n", 
            ctx->my_pokemon, ctx->my_hp, 
            ctx->opponent_pokemon[0] ? ctx->opponent_pokemon : "???", ctx->opponent_hp);
    }
    printf("Status: %s | Turn: %s\n", 
        ctx->state == STATE_WAITING_FOR_MOVE ? "Waiting" : "Processing",
        ctx->is_my_turn ? "MY TURN" : "OPPONENT'S TURN");
    printf("========================================\n");
    
    // Only show moves to players, not spectators
    if (ctx->my_role != ROLE_SPECTATOR && ctx->is_my_turn && ctx->state == STATE_WAITING_FOR_MOVE) {
        printf("Available Moves: Tackle, Ember, Water Gun\n");
    }
    print_prompt(ctx);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <host/join/spectate> <MyPort> [TargetIP] [TargetPort]\n", argv[0]);
        return 1;
    }

    srand(time(NULL));
    int my_port = atoi(argv[2]);
    if (!net_init(my_port)) return 1;

    BattleContext ctx;
    PlayerRole role;
    
    if (strcmp(argv[1], "host") == 0) role = ROLE_HOST;
    else if (strcmp(argv[1], "spectate") == 0) role = ROLE_SPECTATOR;
    else role = ROLE_CLIENT;

    init_battle(&ctx, role, role == ROLE_HOST ? "Charizard" : "Blastoise");

    if (role == ROLE_CLIENT) {
        net_set_peer(argv[3], atoi(argv[4]));
        net_send_game_message("HANDSHAKE_REQUEST", NULL);
        printf("[MAIN] Sending Join Request...\n");
    } 
    else if (role == ROLE_SPECTATOR) {
        net_set_peer(argv[3], atoi(argv[4]));
        net_send_game_message("SPECTATOR_REQUEST", NULL);
        printf("[MAIN] Sending Spectator Request...\n");
    }
    else {
        printf("[MAIN] Hosting on port %d...\n", my_port);
    }

    char input_buffer[100];
    GameMessage msg;

    while (ctx.state != STATE_GAME_OVER) {
        // --- Network Processing ---
        if (net_process_updates(&msg)) {
            printf("\r                                         \r"); // Clear line
            
            // Host handling Joiner
            if (strcmp(msg.message_type, "HANDSHAKE_REQUEST") == 0) {
                net_send_game_message("HANDSHAKE_RESPONSE", "seed: 12345\n");
                char setup[64]; snprintf(setup, 64, "attacker: %s\n", ctx.my_pokemon);
                net_send_game_message("BATTLE_SETUP", setup);
            }
            // Host handling Spectator (Just acknowledge or ignore logic)
            else if (strcmp(msg.message_type, "SPECTATOR_REQUEST") == 0) {
                printf("[NET] Spectator watching.\n");
            }
            // Client/Spectator handling Host Response
            else if (strcmp(msg.message_type, "HANDSHAKE_RESPONSE") == 0) {
                if (role == ROLE_CLIENT) {
                    char setup[64]; snprintf(setup, 64, "attacker: %s\n", ctx.my_pokemon);
                    net_send_game_message("BATTLE_SETUP", setup);
                }
            }
            else {
                // Chat handling
                ChatMessage cmsg;
                if(parse_chat_message(msg.raw_buffer, &cmsg)){
                    printf("\r");
                    display_chat_message(&cmsg);
                    print_prompt(&ctx);
                    continue;
                }   
            
                process_incoming_message(&ctx, &msg);
                print_battle_status(&ctx);
            }
            print_prompt(&ctx);
        }

       if (kbhit_check()) {
            if (fgets(input_buffer, sizeof(input_buffer), stdin)) {
                input_buffer[strcspn(input_buffer, "\n")] = 0;
                
                if (ctx.my_role == ROLE_SPECTATOR) {
                    printf("\r[CHAT] Spectator: %s\n", input_buffer);
                    net_send_chat("Spectator", input_buffer);
                    print_prompt(&ctx);
                }
                else if (ctx.is_my_turn && ctx.state == STATE_WAITING_FOR_MOVE) {
                    execute_move_command(&ctx, input_buffer);
                } 
                else {
                    printf("\r[CHAT] You: %s\n", input_buffer);
                    net_send_chat(role == ROLE_HOST ? "Host" : "Joiner", input_buffer);
                    print_prompt(&ctx);
                }
            }
        }
        sleep_ms(10);
    }

    printf("\nGAME OVER! Winner: %s\n", ctx.my_hp > 0 ? "You" : "Opponent");
    net_cleanup();
    return 0;

}

