#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "damage_calc.h" // Include first to avoid DamageResult conflicts

typedef enum
{
    STATE_SETUP,
    STATE_WAITING_FOR_MOVE,
    STATE_PROCESSING_TURN,
    STATE_GAME_OVER
} BattleState;

typedef enum
{
    ROLE_HOST,
    ROLE_CLIENT,
    ROLE_SPECTATOR
} PlayerRole;

typedef struct
{
    char message_type[32];
    char move_name[32];
    char attacker[32]; // <--- FIX: missing attribute
    int damage_dealt;
    int defender_hp_remaining;
    char winner[32];
    char raw_buffer[4096];
} GameMessage;

typedef struct
{
    BattleState state;
    PlayerRole my_role;
    bool is_my_turn;

    char my_pokemon[32];
    char opponent_pokemon[32];
    int my_hp;
    int opponent_hp;

    char current_move[32];
    char current_attacker[32];
    DamageResult local_calc_result;
    DamageResult remote_calc_report;
} BattleContext;

// Public interfaces
void init_battle(BattleContext *ctx, PlayerRole role, const char *pokemon_name);
void execute_move_command(BattleContext *ctx, const char *move_name);
void process_incoming_message(BattleContext *ctx, GameMessage *msg);

// Network simulation helpers
void send_packet(const char *format, ...);
void set_shared_rng_seed(unsigned int seed);

#endif