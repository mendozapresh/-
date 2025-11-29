#ifndef DAMAGE_CALC_H
#define DAMAGE_CALC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define MAX_POKEMON_NAME 32
#define MAX_TYPE_NAME 16
#define MAX_MOVE_NAME 32

#define MAX_POKEMON 256
#define MAX_MOVES 256

// ---- POKEMON STATS ----
typedef struct
{
    char name[MAX_POKEMON_NAME];
    char type1[MAX_TYPE_NAME];
    char type2[MAX_TYPE_NAME];

    int hp;
    int attack;
    int defense;
    int sp_attack;
    int sp_defense;
    int speed;
} PokemonData;

// ---- MOVE DATA ----
typedef enum
{
    MOVE_PHYSICAL,
    MOVE_SPECIAL,
    MOVE_STATUS
} MoveCategory;

typedef struct
{
    char name[MAX_MOVE_NAME];
    char type[MAX_TYPE_NAME];
    MoveCategory category;
    int power;
} MoveData;

// ---- BOOSTS ----
typedef struct
{
    int attack_boost;
    int defense_boost;
    int sp_attack_boost;
    int sp_defense_boost;
} StatBoosts;

// ---- DAMAGE RESULT ----
typedef struct
{
    int damage_dealt;
    int defender_remaining_hp;
    char status_message[128];
} DamageResult;

// ---- FUNCTION PROTOTYPES ----
void load_pokemon_data(const char *csv_path);
const PokemonData *get_pokemon(const char *name);

void load_moves_csv(const char *csv_path);
const MoveData *get_move(const char *move_name);

int apply_boost(int base_stat, int boost_stage);
float get_type_multiplier(const char *move_type, const char *def_type1, const char *def_type2);

DamageResult calculate_damage_logic(const char *attacker_name, const char *defender_name, const char *move_name);

extern PokemonData POKEMON_DB[MAX_POKEMON];
extern int POKEMON_COUNT;

extern MoveData MOVE_DB[MAX_MOVES];
extern int MOVE_COUNT;

#endif
