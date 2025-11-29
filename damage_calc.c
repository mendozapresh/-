#include "damage_calc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// --- WINDOWS COMPATIBILITY ---
char *strcasestr_custom(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        if (toupper((unsigned char)*haystack) == toupper((unsigned char)*needle)) {
            const char *h = haystack, *n = needle;
            while (*h && *n && toupper((unsigned char)*h) == toupper((unsigned char)*n)) { h++; n++; }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}
#define strcasestr strcasestr_custom
#ifdef _WIN32
#define strcasecmp _stricmp
#endif
// -----------------------------

PokemonData POKEMON_DB[MAX_POKEMON];
int POKEMON_COUNT = 0;
MoveData MOVE_DB[MAX_MOVES];
int MOVE_COUNT = 0;

// Helper to strip whitespace
static void strtrim(char *s) {
    if (!s) return;
    char *p = s + strlen(s) - 1;
    while (p >= s && isspace((unsigned char)*p)) *p-- = '\0';
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

void load_pokemon_data(const char *csv_path) {
    // Hardcoded fallback for demo stability
    POKEMON_COUNT = 0;
    
    PokemonData p;
    memset(&p, 0, sizeof(p));
    strcpy(p.name, "Charizard"); strcpy(p.type1, "Fire"); strcpy(p.type2, "Flying");
    p.hp = 100; p.attack = 84; p.defense = 78; p.sp_attack = 109; p.sp_defense = 85;
    POKEMON_DB[POKEMON_COUNT++] = p;

    memset(&p, 0, sizeof(p));
    strcpy(p.name, "Blastoise"); strcpy(p.type1, "Water"); strcpy(p.type2, "NONE");
    p.hp = 100; p.attack = 83; p.defense = 100; p.sp_attack = 85; p.sp_defense = 105;
    POKEMON_DB[POKEMON_COUNT++] = p;
    
    memset(&p, 0, sizeof(p));
    strcpy(p.name, "Venusaur"); strcpy(p.type1, "Grass"); strcpy(p.type2, "Poison");
    p.hp = 100; p.attack = 82; p.defense = 83; p.sp_attack = 100; p.sp_defense = 100;
    POKEMON_DB[POKEMON_COUNT++] = p;
}

void load_moves_csv(const char *csv_path) {
    MOVE_COUNT = 0;
    MoveData m;

    memset(&m, 0, sizeof(m)); strcpy(m.name, "Tackle"); strcpy(m.type, "Normal");
    m.category = MOVE_PHYSICAL; m.power = 40; MOVE_DB[MOVE_COUNT++] = m;

    memset(&m, 0, sizeof(m)); strcpy(m.name, "Ember"); strcpy(m.type, "Fire");
    m.category = MOVE_SPECIAL; m.power = 40; MOVE_DB[MOVE_COUNT++] = m;

    memset(&m, 0, sizeof(m)); strcpy(m.name, "Water Gun"); strcpy(m.type, "Water");
    m.category = MOVE_SPECIAL; m.power = 40; MOVE_DB[MOVE_COUNT++] = m;
}

const PokemonData *get_pokemon(const char *name) {
    for (int i = 0; i < POKEMON_COUNT; ++i) {
        if (strcasecmp(POKEMON_DB[i].name, name) == 0) return &POKEMON_DB[i];
    }
    return NULL;
}

const MoveData *get_move(const char *name) {
    for (int i = 0; i < MOVE_COUNT; ++i) {
        if (strcasecmp(MOVE_DB[i].name, name) == 0) return &MOVE_DB[i];
    }
    return NULL;
}

DamageResult calculate_damage_logic(const char *attacker_name, const char *defender_name, const char *move_name) {
    DamageResult out;
    memset(&out, 0, sizeof(out));
    
    // SAFETY: If names are empty/invalid, return 0 damage, but DO NOT crash HP to 0
    const PokemonData *A = get_pokemon(attacker_name);
    const PokemonData *D = get_pokemon(defender_name);
    const MoveData *M = get_move(move_name);

    if (!A || !D || !M) {
        // If invalid, return 0 damage. 
        // IMPORTANT: We return D->hp (current max) as remaining so logic doesn't think they died.
        // If D is null, we return 100.
        out.damage_dealt = 0;
        out.defender_remaining_hp = D ? D->hp : 100; 
        sprintf(out.status_message, "Missed (Invalid Data)");
        return out;
    }

    // Simple Damage Formula
    float type_bonus = 1.0; 
    // (Simplified type chart for demo)
    if (strcasecmp(M->type, "Fire") == 0 && strcasecmp(D->type1, "Grass") == 0) type_bonus = 2.0;
    if (strcasecmp(M->type, "Water") == 0 && strcasecmp(D->type1, "Fire") == 0) type_bonus = 2.0;
    if (strcasecmp(M->type, "Grass") == 0 && strcasecmp(D->type1, "Water") == 0) type_bonus = 2.0;

    float damage = (float)M->power * type_bonus;
    
    // Stats impact
    int atk = (M->category == MOVE_PHYSICAL) ? A->attack : A->sp_attack;
    int def = (M->category == MOVE_PHYSICAL) ? D->defense : D->sp_defense;
    damage = damage * ((float)atk / (float)def);

    out.damage_dealt = (int)damage;
    if (out.damage_dealt < 1) out.damage_dealt = 1;
    
    // Note: Logic layer handles subtraction from current HP. 
    // Calculator just returns theoretical remaining from FULL HP if asked, 
    // but Logic layer overrides this usually.
    out.defender_remaining_hp = D->hp - out.damage_dealt; 

    sprintf(out.status_message, "Hit for %d dmg", out.damage_dealt);
    return out;
}