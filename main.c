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
#define sleep_ms(x) usleep((x) * 1000)
int kbhit_check()
{
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}
#endif

// --- Selection Constants ---
#define TYPE1_COLUMN_INDEX 36
#define NAME_COLUMN_INDEX 30
#define MAX_POKEMON_COUNT 801
#define MAX_NAME_LEN 32
#define MAX_CLASS_LEN 64 // Used for type name strings
#define NUM_CLASSES_TO_USE 10
// ---------------------------

// --- ROBUST CSV PARSING HELPER FOR SELECTION SCREEN ---

static char *select_find_field(char *line, int index)
{
    char *p = line;
    for (int i = 0; i < index; i++)
    {
        if (*p == '\0')
            return NULL;

        if (*p == '"')
        {
            p++;
            p = strchr(p, '"');

            while (p && p[1] != ',' && p[1] != '\0')
            {
                p = strchr(p + 1, '"');
            }

            if (!p)
                return NULL;
            p++;
        }

        p = strchr(p, ',');
        if (!p)
            return NULL;
        p++;
    }
    return p;
}

static void select_extract_field(char *src, char *dest, size_t max_len)
{
    char *end = src;
    while (*end != '\0' && *end != ',' && *end != '\n' && *end != '\r')
    {
        end++;
    }

    size_t len = end - src;
    if (len >= max_len)
        len = max_len - 1;

    strncpy(dest, src, len);
    dest[len] = '\0';

    char *start_trim = dest;
    char *end_trim = dest + strlen(dest) - 1;

    if (*start_trim == '"')
        start_trim++;

    if (*end_trim == '"' && end_trim >= start_trim)
        *end_trim-- = '\0';

    while (isspace((unsigned char)*start_trim))
        start_trim++;
    while (end_trim >= start_trim && isspace((unsigned char)*end_trim))
        *end_trim-- = '\0';

    if (start_trim != dest)
    {
        memmove(dest, start_trim, strlen(start_trim) + 1);
    }
}

// Renamed 'classes' parameter to 'types' for clarity
int load_pokemon_data_for_selection(char names[MAX_POKEMON_COUNT][MAX_NAME_LEN], char types[MAX_POKEMON_COUNT][MAX_CLASS_LEN])
{
    FILE *file = fopen("pokemon.csv", "r");
    if (file == NULL)
    {
        fprintf(stderr, "[FATAL ERROR] Cannot open pokemon.csv. Using minimal hardcoded list for selection.\n");
        // Fallback: use actual types for consistency with filtering logic
        strncpy(names[0], "Bulbasaur", MAX_NAME_LEN);
        strncpy(types[0], "grass", MAX_CLASS_LEN);
        strncpy(names[1], "Charmander", MAX_NAME_LEN);
        strncpy(types[1], "fire", MAX_CLASS_LEN);
        strncpy(names[2], "Squirtle", MAX_NAME_LEN);
        strncpy(types[2], "water", MAX_CLASS_LEN);
        return 3;
    }

    char line[8192];
    int count = 0;

    if (fgets(line, sizeof(line), file) == NULL)
    {
        fclose(file);
        return 0;
    } // Discard header

    while (fgets(line, sizeof(line), file) && count < MAX_POKEMON_COUNT)
    {
        char temp_type[MAX_CLASS_LEN];
        char temp_name[MAX_NAME_LEN];

        char line_copy_1[8192];
        char line_copy_2[8192];
        strncpy(line_copy_1, line, sizeof(line_copy_1) - 1);
        line_copy_1[sizeof(line_copy_1) - 1] = '\0';
        strncpy(line_copy_2, line, sizeof(line_copy_2) - 1);
        line_copy_2[sizeof(line_copy_2) - 1] = '\0';

        // Extract Type1 (Index 36 - Matches IDX_TYPE1 in damage_calc.c)
        if (select_find_field(line_copy_1, TYPE1_COLUMN_INDEX) == NULL)
            continue;
        select_extract_field(select_find_field(line_copy_1, TYPE1_COLUMN_INDEX), temp_type, MAX_CLASS_LEN);

        // Extract Name (Index 30)
        if (select_find_field(line_copy_2, NAME_COLUMN_INDEX) == NULL)
            continue;
        select_extract_field(select_find_field(line_copy_2, NAME_COLUMN_INDEX), temp_name, MAX_NAME_LEN);

        if (temp_name[0] == '\0' || temp_type[0] == '\0')
            continue;

        strncpy(types[count], temp_type, MAX_CLASS_LEN - 1);
        types[count][MAX_CLASS_LEN - 1] = '\0';
        strncpy(names[count], temp_name, MAX_NAME_LEN - 1);
        names[count][MAX_NAME_LEN - 1] = '\0';
        count++;
    }

    fclose(file);
    return count;
}

void print_prompt(BattleContext *ctx)
{
    if (ctx->is_my_turn && ctx->state == STATE_WAITING_FOR_MOVE)
    {
        printf("\rAction> ");
        fflush(stdout);
    }
}

void print_battle_status(BattleContext *ctx)
{
    printf("\n========================================\n");
    if (ctx->my_role == ROLE_SPECTATOR)
    {
        printf("      --- SPECTATOR MODE ---\n");
        printf("P1 (%s): %d HP  VS  P2 (%s): %d HP\n",
               ctx->my_pokemon, ctx->my_hp,
               ctx->opponent_pokemon, ctx->opponent_hp); // Spectator tracks both as "my" and "opponent" generic slots
    }
    else
    {
        printf("ME (%s): %d HP  VS  OPPONENT (%s): %d HP\n",
               ctx->my_pokemon, ctx->my_hp,
               ctx->opponent_pokemon[0] ? ctx->opponent_pokemon : "???", ctx->opponent_hp);
    }
    printf("Status: %s | Turn: %s\n",
           ctx->state == STATE_WAITING_FOR_MOVE ? "Waiting" : "Processing",
           ctx->is_my_turn ? "MY TURN" : "OPPONENT'S TURN");
    printf("========================================\n");

    // Only show moves to players, not spectators, and fetch abilities for moves
    if (ctx->my_role != ROLE_SPECTATOR && ctx->is_my_turn && ctx->state == STATE_WAITING_FOR_MOVE)
    {
        const PokemonData *p = get_pokemon(ctx->my_pokemon);

        if (p && p->ability_count > 0)
        {
            printf("Available Moves (Abilities): ");
            bool first = true;
            for (int i = 0; i < p->ability_count; i++)
            {
                if (p->abilities[i][0] == '\0')
                    continue; // skip empty
                if (!first)
                    printf(", ");
                printf("%s", p->abilities[i]);
                first = false;
            }
            printf("\n");
        }
        else
        {
            printf("Available Moves: Abilities not loaded or Pokémon not found.\n");
        }
    }
    print_prompt(ctx);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <host/join/spectate> <MyPort> [TargetIP] [TargetPort]\n", argv[0]);
        return 1;
    }

    srand(time(NULL));
    int my_port = atoi(argv[2]);
    if (!net_init(my_port))
        return 1;

    BattleContext ctx;
    PlayerRole role;

    if (strcmp(argv[1], "host") == 0)
        role = ROLE_HOST;
    else if (strcmp(argv[1], "spectate") == 0)
        role = ROLE_SPECTATOR;
    else
        role = ROLE_CLIENT;

    // --- POKEMON SELECTION LOGIC START ---

    char all_names[MAX_POKEMON_COUNT][MAX_NAME_LEN];
    char all_types[MAX_POKEMON_COUNT][MAX_CLASS_LEN]; // Renamed for clarity: stores the Type1 name
    int total_pokemon = load_pokemon_data_for_selection(all_names, all_types);

    // Spectators don't pick
    char pokemon_name_buffer[32] = "SPECTATOR_UNIT";

    if (role != ROLE_SPECTATOR)
    {
        // Ensure fallback kicks in properly
        if (total_pokemon <= 0)
        {
            fprintf(stderr, "[FATAL] Pokémon data unavailable.\n");
            net_cleanup();
            return 1;
        }

        // -------- STAGE 1: PICK TYPE --------
        char unique_types[NUM_CLASSES_TO_USE][MAX_CLASS_LEN]; // Renamed for clarity
        int unique_count = 0;

        for (int i = 0; i < total_pokemon && unique_count < NUM_CLASSES_TO_USE; i++)
        {
            int repeat = 0;
            for (int j = 0; j < unique_count; j++)
            {
                if (strcmp(all_types[i], unique_types[j]) == 0)
                {
                    repeat = 1;
                    break;
                }
            }
            if (!repeat)
            {
                strncpy(unique_types[unique_count], all_types[i], MAX_CLASS_LEN - 1);
                unique_types[unique_count][MAX_CLASS_LEN - 1] = '\0';
                unique_count++;
            }
        }

        // Display type choices
        int type_choice = -1; // Renamed for clarity
        char input_buffer[16];

        do
        {
            printf("\n--- STAGE 1: Choose type (First %d Loaded) ---\n", unique_count);
            for (int i = 0; i < unique_count; i++)
                printf("%d) %s\n", i + 1, unique_types[i]);

            printf("Enter the number of a type (1-%d): ", unique_count);

            if (!fgets(input_buffer, sizeof(input_buffer), stdin))
            {
                net_cleanup();
                return 1;
            }

            input_buffer[strcspn(input_buffer, "\n")] = 0;
            type_choice = atoi(input_buffer); // Renamed for clarity

            if (type_choice < 1 || type_choice > unique_count)
                printf("\n*** Invalid choice. Try 1-%d ***\n", unique_count);

        } while (type_choice < 1 || type_choice > unique_count);

        const char *chosen_type = unique_types[type_choice - 1]; // Renamed for clarity

        // -------- STAGE 2: PICK POKÉMON NAME --------

        char filtered_names[MAX_POKEMON_COUNT][MAX_NAME_LEN];
        int filtered_count = 0;

        for (int i = 0; i < total_pokemon; i++)
        {
            if (strcmp(all_types[i], chosen_type) == 0)
            {
                strncpy(filtered_names[filtered_count], all_names[i], MAX_NAME_LEN - 1);
                filtered_names[filtered_count][MAX_NAME_LEN - 1] = '\0';
                filtered_count++;
            }
        }

        if (filtered_count == 0)
        {
            fprintf(stderr, "[ERROR] No Pokémon with this type.\n");
            net_cleanup();
            return 1;
        }

        int pokemon_choice = -1;

        do
        {
            printf("\n--- STAGE 2: Choose a %s Pokémon (%d available) ---\n",
                   chosen_type, filtered_count); // Uses chosen_type

            for (int i = 0; i < filtered_count; i++)
                printf("%d) %s\n", i + 1, filtered_names[i]);

            printf("Enter choice (1-%d): ", filtered_count);

            if (!fgets(input_buffer, sizeof(input_buffer), stdin))
            {
                net_cleanup();
                return 1;
            }

            input_buffer[strcspn(input_buffer, "\n")] = 0;
            pokemon_choice = atoi(input_buffer);

            if (pokemon_choice < 1 || pokemon_choice > filtered_count)
                printf("\n*** Invalid choice. Try 1-%d ***\n", filtered_count);

        } while (pokemon_choice < 1 || pokemon_choice > filtered_count);

        strncpy(pokemon_name_buffer, filtered_names[pokemon_choice - 1], 31);
        pokemon_name_buffer[31] = '\0';
    }

    load_all_pokemon_and_moves("pokemon.csv");
    init_battle(&ctx, role, pokemon_name_buffer);
    // --- POKEMON SELECTION LOGIC END ---

    if (role == ROLE_CLIENT)
    {
        net_set_peer(argv[3], atoi(argv[4]));
        net_send_game_message("HANDSHAKE_REQUEST", NULL);
        printf("[MAIN] Sending Join Request...\n");
    }
    else if (role == ROLE_SPECTATOR)
    {
        net_set_peer(argv[3], atoi(argv[4]));
        net_send_game_message("SPECTATOR_REQUEST", NULL);
        printf("[MAIN] Sending Spectator Request...\n");
    }
    else
    {
        printf("[MAIN] Hosting on port %d...\n", my_port);
    }

    char input_buffer[100];
    GameMessage msg;

    while (ctx.state != STATE_GAME_OVER)
    {
        // --- NETWORK POLLING ---
        if (net_process_updates(&msg))
        {
            printf("\r                                     \r"); // Clear line

            // Handshake logic
            if (strcmp(msg.message_type, "HANDSHAKE_REQUEST") == 0)
            {
                net_send_game_message("HANDSHAKE_RESPONSE", "seed: 12345\n");
                char setup[64];
                snprintf(setup, sizeof(setup), "attacker: %s\n", ctx.my_pokemon);
                net_send_game_message("BATTLE_SETUP", setup);
            }
            else if (strcmp(msg.message_type, "SPECTATOR_REQUEST") == 0)
            {
                printf("[NET] Spectator has joined.\n");
            }
            else if (strcmp(msg.message_type, "HANDSHAKE_RESPONSE") == 0)
            {
                if (role == ROLE_CLIENT)
                {
                    char setup[64];
                    snprintf(setup, sizeof(setup), "attacker: %s\n", ctx.my_pokemon);
                    net_send_game_message("BATTLE_SETUP", setup);
                }
            }
            else
            {
                // Chat?
                ChatMessage cmsg;
                if (parse_chat_message(msg.raw_buffer, &cmsg))
                {
                    printf("\r");
                    display_chat_message(&cmsg);
                    print_prompt(&ctx);
                    continue;
                }

                // Game event
                process_incoming_message(&ctx, &msg);
                print_battle_status(&ctx);
            }

            print_prompt(&ctx);
        }

        // --- PLAYER INPUT ---
        if (kbhit_check())
        {
            if (fgets(input_buffer, sizeof(input_buffer), stdin))
            {
                input_buffer[strcspn(input_buffer, "\n")] = 0;

                if (ctx.my_role == ROLE_SPECTATOR)
                {
                    printf("\r[CHAT] Spectator: %s\n", input_buffer);
                    net_send_chat("Spectator", input_buffer);
                }
                else if (ctx.is_my_turn && ctx.state == STATE_WAITING_FOR_MOVE)
                {
                    execute_move_command(&ctx, input_buffer);
                }
                else
                {
                    printf("\r[CHAT] You: %s\n", input_buffer);
                    net_send_chat(role == ROLE_HOST ? "Host" : "Joiner", input_buffer);
                }

                print_prompt(&ctx);
            }
        }

        sleep_ms(10);
    }

    printf("\nGAME OVER! Winner: %s\n", ctx.my_hp > 0 ? "You" : "Opponent");
    net_cleanup();
    return 0;
}