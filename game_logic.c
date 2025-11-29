#include "game_logic.h"
#include "damage_calc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms_logic(x) Sleep(x)
#else
#include <unistd.h>
#define sleep_ms_logic(x) usleep((x) * 1000)
#endif

extern void network_send_message(const char *msg);
extern int network_get_next_sequence(void);

unsigned int shared_rng_seed = 0;
void set_shared_rng_seed(unsigned int seed) { shared_rng_seed = seed; }

void init_battle(BattleContext *ctx, PlayerRole role, const char *pokemon_name)
{
    // 1. Load Pokémon stats and abilities into the database (POKEMON_DB)
    load_pokemon_data("pokemon.csv");
    // 2. Populate the move database (MOVE_DB) using abilities from the Pokémon data.
    // This makes the Pokémon's abilities the available moves for use in battle.
    load_moves_from_pokemon();
    // 3. (Optional, Removed) load_moves_csv("moves.csv"); - No longer needed as abilities serve as moves.

    memset(ctx, 0, sizeof(BattleContext));
    ctx->my_role = role;
    strncpy(ctx->my_pokemon, pokemon_name, 31);
    const PokemonData *mine = get_pokemon(pokemon_name);
    ctx->my_hp = mine ? mine->hp : 100;
    ctx->opponent_hp = 100;
    ctx->state = STATE_SETUP;
    ctx->is_my_turn = (role == ROLE_HOST);
    printf("[LOGIC] Battle Init. Me: %s (%d HP). State: SETUP\n", ctx->my_pokemon, ctx->my_hp);
}

void perform_turn_calculation(BattleContext *ctx)
{
    if (strlen(ctx->current_attacker) == 0)
        return;

    bool i_am_attacker = (strcmp(ctx->current_attacker, ctx->my_pokemon) == 0);
    const char *attacker = i_am_attacker ? ctx->my_pokemon : ctx->opponent_pokemon;
    const char *defender = i_am_attacker ? ctx->opponent_pokemon : ctx->my_pokemon;
    int current_def_hp = i_am_attacker ? ctx->opponent_hp : ctx->my_hp;

    DamageResult res = calculate_damage_logic(attacker, defender, ctx->current_move);
    int new_hp = current_def_hp - res.damage_dealt;
    if (new_hp < 0)
        new_hp = 0;

    ctx->local_calc_result = res;
    ctx->local_calc_result.defender_remaining_hp = new_hp;

    printf("[LOGIC] Calc: %s used %s on %s. Dmg: %d, OldHP: %d, NewHP: %d\n",
           attacker, ctx->current_move, defender, res.damage_dealt, current_def_hp, new_hp);

    char payload[512];
    snprintf(payload, sizeof(payload),
             "message_type: CALCULATION_REPORT\n"
             "attacker: %s\nmove_used: %s\ndamage_dealt: %d\ndefender_hp_remaining: %d\n"
             "sequence_number: %d\n",
             ctx->current_attacker, ctx->current_move, res.damage_dealt, new_hp, network_get_next_sequence());
    network_send_message(payload);
}

void finalize_turn(BattleContext *ctx)
{
    if (ctx->opponent_hp <= 0 || ctx->my_hp <= 0)
    {
        ctx->state = STATE_GAME_OVER;
        printf("[LOGIC] GAME OVER. Me: %d, Opp: %d\n", ctx->my_hp, ctx->opponent_hp);
        return;
    }
    ctx->is_my_turn = !ctx->is_my_turn;
    ctx->state = STATE_WAITING_FOR_MOVE;
    printf("[LOGIC] Turn End. Next: %s\n", ctx->is_my_turn ? "MY TURN" : "OPPONENT");
}

void handle_battle_setup(BattleContext *ctx, GameMessage *msg)
{
    if (strlen(msg->attacker) > 0)
    {
        strncpy(ctx->opponent_pokemon, msg->attacker, 31);
        const PokemonData *opp = get_pokemon(ctx->opponent_pokemon);
        if (opp)
            ctx->opponent_hp = opp->hp;
        printf("[LOGIC] Opponent is %s (%d HP)\n", ctx->opponent_pokemon, ctx->opponent_hp);
        ctx->state = STATE_WAITING_FOR_MOVE;
    }
}

void handle_attack_announce(BattleContext *ctx, GameMessage *msg)
{
    if (strcmp(msg->attacker, ctx->my_pokemon) == 0)
        return; // Ignore my own echo

    strncpy(ctx->current_move, msg->move_name, 31);
    strncpy(ctx->current_attacker, ctx->opponent_pokemon, 31);
    printf("[LOGIC] Opponent attacks with %s\n", msg->move_name);
    ctx->state = STATE_PROCESSING_TURN;
    perform_turn_calculation(ctx);
}

void handle_calculation_report(BattleContext *ctx, GameMessage *msg)
{
    // --- SAFETY: Catch-up if we missed ATTACK_ANNOUNCE ---
    if (ctx->state == STATE_WAITING_FOR_MOVE && strcmp(msg->attacker, ctx->opponent_pokemon) == 0)
    {
        printf("[LOGIC] Warning: Missed ATTACK_ANNOUNCE. Catching up state...\n");
        strncpy(ctx->current_attacker, msg->attacker, 31);
        strncpy(ctx->current_move, msg->move_name, 31); // Use move_name from struct
        ctx->state = STATE_PROCESSING_TURN;
        perform_turn_calculation(ctx);
        // We just ran our calc, now we continue to compare
    }

    printf("[LOGIC] Report Check. Me: Dmg %d | Opp: Dmg %d\n",
           ctx->local_calc_result.damage_dealt, msg->damage_dealt);

    char payload[256];
    snprintf(payload, sizeof(payload), "message_type: CALCULATION_CONFIRM\nsequence_number: %d\n", network_get_next_sequence());
    network_send_message(payload);

    if (strcmp(msg->attacker, ctx->my_pokemon) == 0)
        ctx->opponent_hp = msg->defender_hp_remaining;
    else
        ctx->my_hp = msg->defender_hp_remaining;

    finalize_turn(ctx);
}

void execute_move_command(BattleContext *ctx, const char *move_name)
{
    if (!ctx->is_my_turn || ctx->state != STATE_WAITING_FOR_MOVE)
        return;

    strncpy(ctx->current_move, move_name, 31);
    strncpy(ctx->current_attacker, ctx->my_pokemon, 31);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "message_type: ATTACK_ANNOUNCE\nmove_name: %s\nsequence_number: %d\n",
             move_name, network_get_next_sequence());
    network_send_message(payload);

    sleep_ms_logic(100); // Prevent packet storm race condition
    ctx->state = STATE_PROCESSING_TURN;
    perform_turn_calculation(ctx);
}

void process_incoming_message(BattleContext *ctx, GameMessage *msg)
{
    if (strcmp(msg->message_type, "BATTLE_SETUP") == 0)
        handle_battle_setup(ctx, msg);
    else if (strcmp(msg->message_type, "ATTACK_ANNOUNCE") == 0)
        handle_attack_announce(ctx, msg);
    else if (strcmp(msg->message_type, "CALCULATION_REPORT") == 0)
        handle_calculation_report(ctx, msg);
    // REMOVED finalize_turn from CALCULATION_CONFIRM to prevent Double Toggle bug
}