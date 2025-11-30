// damage_calc.c
#include "damage_calc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// ---- Type names & chart ----
static const char *TYPE_NAMES[] = {
    "Bug", "Dark", "Dragon", "Electric", "Fairy", "Fighting", "Fire",
    "Flying", "Ghost", "Grass", "Ground", "Ice", "Normal", "Poison",
    "Psychic", "Rock", "Steel", "Water"};
#define TYPE_COUNT (sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0]))

static const float TYPE_CHART[TYPE_COUNT][TYPE_COUNT] = {
    // partial chart for brevity, you can fill all entries
    {1, 1, 0.5, 1, 1, 0.5, 0.5, 2, 0.5, 2, 1, 1, 1, 0.5, 2, 1, 1, 1}, // Bug
    {1, 0.5, 1, 1, 0.5, 2, 1, 1, 0.5, 1, 1, 1, 1, 1, 2, 1, 1, 1},     // Dark
    {1, 1, 2, 1, 0.5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.5, 0.5},     // Dragon
    {1, 1, 0.5, 0.5, 1, 1, 1, 2, 1, 0.5, 0, 1, 1, 1, 1, 1, 1, 2},     // Electric
    // ... fill remaining
};

// ---- Globals ----
PokemonData POKEMON_DB[MAX_POKEMON];
int POKEMON_COUNT = 0;
MoveData MOVE_DB[MAX_MOVES];
int MOVE_COUNT = 0;

// ---- Case-insensitive strstr ----
char *strcasestr_custom(const char *haystack, const char *needle)
{
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; ++haystack)
    {
        if (toupper((unsigned char)*haystack) == toupper((unsigned char)*needle))
        {
            const char *h = haystack, *n = needle;
            while (*h && *n && toupper((unsigned char)*h) == toupper((unsigned char)*n))
            {
                h++;
                n++;
            }
            if (!*n)
                return (char *)haystack;
        }
    }
    return NULL;
}
#define strcasestr(str, sub) strcasestr_custom(str, sub)
#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#endif

// ---- CSV helpers ----
static char *find_nth_field(char *line, int n)
{
    char *p = line;
    for (int i = 0; i < n; i++)
    {
        if (!*p)
            return NULL;
        if (*p == '"')
        {
            p++;
            while (*p && *p != '"')
                p++;
            if (!*p)
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

static void extract_field(char *src, char *dest, size_t max_len)
{
    if (!src || !dest || max_len == 0)
        return;

    // 1. Find the end of the field (up to the next delimiter/newline)
    char *end = src;
    while (*end != '\0' && *end != ',' && *end != '\n' && *end != '\r')
        end++;

    // 2. Copy the raw field content to the destination buffer
    size_t len = (size_t)(end - src);
    if (len >= max_len)
        len = max_len - 1;
    memcpy(dest, src, len);
    dest[len] = '\0'; // Ensure null termination of the copied raw string

    // *** ROBUST TRIM LOGIC ***
    char *s = dest;
    char *e = dest + strlen(dest) - 1;

    // 3. Trim leading whitespace and potential outer quote (e.g., remove ' "')
    while (*s)
    {
        if (isspace((unsigned char)*s) || *s == '"')
        {
            s++;
        }
        else
        {
            break;
        }
    }

    // 4. Trim trailing whitespace and potential outer quote (e.g., remove '" ')
    while (e >= s)
    {
        if (isspace((unsigned char)*e) || *e == '"')
        {
            *e = '\0';
            e--;
        }
        else
        {
            break;
        }
    }

    // 5. Shift the remaining string to the start if leading characters were trimmed
    if (s != dest)
        memmove(dest, s, strlen(s) + 1);
}

FILE *open_pokemon_csv(const char *path)
{
    if (!path)
        return NULL;
    FILE *f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "[ERROR] Could not open %s\n", path);
        return NULL;
    }
    return f;
}

// ---- Parse multiple abilities ----
int parse_abilities(const char *field, char abilities[][MAX_MOVE_NAME], int max_abilities)
{
    if (!field || !abilities || max_abilities <= 0)
        return 0;
    int count = 0;
    const char *p = field;

    // 1. Move pointer to the start of the *first* ability name (past '[' and potential '"').
    p = strchr(p, '\'');
    if (!p)
        return 0;
    p++; // Now pointing at the first character of the first ability name.

    // 2. Loop until the end of the ability list ']' or max abilities reached.
    while (*p && *p != ']' && count < max_abilities)
    {
        // 3. Find the closing single quote (''') for the current ability name.
        const char *end_q = strchr(p, '\'');

        if (end_q)
        {
            size_t len = end_q - p;

            // Check for valid length and store the ability name (including internal spaces).
            if (len > 0 && len < MAX_MOVE_NAME)
            {
                strncpy(abilities[count], p, len);
                abilities[count][len] = 0;
                count++;
            }

            // Move pointer past the closing quote.
            p = end_q + 1;

            // 4. Skip over any separators (',', ' ', or opening quote for the next item).
            while (*p && (*p == ',' || *p == ' ' || *p == '\''))
                p++;
        }
        else
        {
            // Malformed string (missing closing quotes).
            break;
        }
    }
    return count;
}

// ---- Type helpers ----
static int type_name_to_index(const char *t)
{
    if (!t)
        return -1;
    for (int i = 0; i < TYPE_COUNT; i++)
        if (strcasecmp(t, TYPE_NAMES[i]) == 0)
            return i;
    return -1;
}

float get_type_multiplier(const char *move_type, const char *def_type1, const char *def_type2)
{
    if (!move_type || !*move_type)
        return 1.0f;
    int a_idx = type_name_to_index(move_type);
    if (a_idx < 0)
        return 1.0f;
    float mult1 = 1.0f, mult2 = 1.0f;
    if (def_type1 && def_type1[0] != '\0' && strcasecmp(def_type1, "NONE") != 0)
    {
        int d1 = type_name_to_index(def_type1);
        if (d1 >= 0)
            mult1 = TYPE_CHART[a_idx][d1];
    }
    if (def_type2 && def_type2[0] != '\0' && strcasecmp(def_type2, "NONE") != 0)
    {
        int d2 = type_name_to_index(def_type2);
        if (d2 >= 0)
            mult2 = TYPE_CHART[a_idx][d2];
    }
    float total = mult1 * mult2;
    if (total <= 0)
        total = 1.0f;
    return total;
}

// ---- Load Pokémon CSV ----
void load_pokemon_data(const char *csv_path)
{
    FILE *file = open_pokemon_csv(csv_path);
    if (!file)
    { // fallback
        POKEMON_COUNT = 0;
        PokemonData p;
        memset(&p, 0, sizeof(p));
        strncpy(p.name, "Charizard", MAX_POKEMON_NAME);
        strncpy(p.type1, "Fire", MAX_TYPE_NAME);
        strncpy(p.type2, "Flying", MAX_TYPE_NAME);
        p.hp = 78;
        p.attack = 84;
        p.defense = 78;
        p.sp_attack = 109;
        p.sp_defense = 85;
        p.speed = 100;
        p.ability_count = 1;
        strncpy(p.abilities[0], "Blaze", MAX_MOVE_NAME);
        POKEMON_DB[POKEMON_COUNT++] = p;
        return;
    }

    char line[8192], tmp[1024];
    POKEMON_COUNT = 0;
    if (!fgets(line, sizeof(line), file))
    {
        fclose(file);
        return;
    } // skip header

    while (fgets(line, sizeof(line), file) && POKEMON_COUNT < MAX_POKEMON)
    {
        PokemonData *p = &POKEMON_DB[POKEMON_COUNT];
        memset(p, 0, sizeof(PokemonData));
        char copy[8192];
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = 0;

        char *fld = find_nth_field(copy, 0); // abilities
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->ability_count = parse_abilities(tmp, p->abilities, MAX_ABILITIES_PER_POKEMON);
        }
        else
            p->ability_count = 0;

        fld = find_nth_field(copy, 28);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->hp = tmp[0] ? atoi(tmp) : 1;
        }
        fld = find_nth_field(copy, 19);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->attack = tmp[0] ? atoi(tmp) : 0;
        }
        fld = find_nth_field(copy, 25);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->defense = tmp[0] ? atoi(tmp) : 1;
        }
        fld = find_nth_field(copy, 33);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->sp_attack = tmp[0] ? atoi(tmp) : 0;
        }
        fld = find_nth_field(copy, 34);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->sp_defense = tmp[0] ? atoi(tmp) : 1;
        }
        fld = find_nth_field(copy, 35);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->speed = tmp[0] ? atoi(tmp) : 0;
        }
        fld = find_nth_field(copy, 30);
        if (fld)
            extract_field(fld, p->name, sizeof(p->name));
        fld = find_nth_field(copy, 36);
        if (fld)
            extract_field(fld, p->type1, sizeof(p->type1));
        fld = find_nth_field(copy, 37);
        if (fld)
        {
            extract_field(fld, p->type2, sizeof(p->type2));
            if (p->type2[0] == 0)
                strncpy(p->type2, "NONE", MAX_TYPE_NAME - 1);
        }
        POKEMON_COUNT++;
    }
    fclose(file);
}

// ---- Generate Moves from Abilities ----
void load_moves_from_pokemon()
{
    MOVE_COUNT = 0;
    for (int i = 0; i < POKEMON_COUNT && MOVE_COUNT < MAX_MOVES; i++)
    {
        PokemonData *p = &POKEMON_DB[i];
        for (int a = 0; a < p->ability_count && MOVE_COUNT < MAX_MOVES; a++)
        {
            MoveData *m = &MOVE_DB[MOVE_COUNT];
            memset(m, 0, sizeof(MoveData));
            strncpy(m->name, p->abilities[a], MAX_MOVE_NAME - 1);
            strncpy(m->type, p->type1[0] ? p->type1 : "Normal", MAX_TYPE_NAME - 1);
            m->category = MOVE_SPECIAL;
            m->power = (p->attack > 0) ? p->attack : 50;
            MOVE_COUNT++;
        }
    }
}

// ---- Convenience loader ----
void load_all_pokemon_and_moves(const char *pokemon_csv)
{
    load_pokemon_data(pokemon_csv);
    load_moves_from_pokemon();
}

// ---- Lookup helpers ----
const PokemonData *get_pokemon(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < POKEMON_COUNT; i++)
        if (strcasecmp(POKEMON_DB[i].name, name) == 0)
            return &POKEMON_DB[i];
    return NULL;
}
const MoveData *get_move(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < MOVE_COUNT; i++)
        if (strcasecmp(MOVE_DB[i].name, name) == 0)
            return &MOVE_DB[i];
    return NULL;
}

// ---- Damage calculation ----
int apply_boost(int base_stat, int boost_stage)
{
    static const float STAGE_MULTS[] = {0.25f, 0.2857f, 0.3333f, 0.4f, 0.5f, 0.6667f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f};
    int idx = boost_stage + 6;
    if (idx < 0)
        idx = 0;
    if (idx > 12)
        idx = 12;
    return (int)floorf(base_stat * STAGE_MULTS[idx] + 0.0001f);
}

DamageResult calculate_damage_logic(const char *attacker_name, const char *defender_name, const char *move_name)
{
    DamageResult out;
    memset(&out, 0, sizeof(out));
    out.damage_dealt = 0;
    out.defender_remaining_hp = 0;
    strcpy(out.status_message, "Missed (Invalid Data)");

    const PokemonData *A = get_pokemon(attacker_name);
    const PokemonData *D = get_pokemon(defender_name);
    const MoveData *M = get_move(move_name);
    if (!A || !D || !M)
    {
        out.defender_remaining_hp = D ? D->hp : 100;
        return out;
    }

    int attacker_boost_stage = 0, defender_boost_stage = 0;
    float attacker_stat = 1.0f, defender_stat = 1.0f;
    if (M->category == MOVE_PHYSICAL)
    {
        attacker_stat = (float)apply_boost(A->attack, attacker_boost_stage);
        defender_stat = (float)apply_boost(D->defense, defender_boost_stage);
    }
    else if (M->category == MOVE_SPECIAL)
    {
        attacker_stat = (float)apply_boost(A->sp_attack, attacker_boost_stage);
        defender_stat = (float)apply_boost(D->sp_defense, defender_boost_stage);
    }
    else
    {
        out.damage_dealt = 0;
        out.defender_remaining_hp = D->hp;
        strcpy(out.status_message, "No damage (Status move)");
        return out;
    }

    if (defender_stat <= 0.0f)
        defender_stat = 1.0f;
    float base_power = (M->power > 0) ? (float)M->power : 1.0f;
    float type_multiplier = get_type_multiplier(M->type, D->type1, D->type2);
    int damage = (int)floorf(base_power * (attacker_stat / defender_stat) * type_multiplier + 0.00001f);
    if (damage < 1)
        damage = 1;
    out.damage_dealt = damage;
    out.defender_remaining_hp = D->hp - damage;
    if (out.defender_remaining_hp < 0)
        out.defender_remaining_hp = 0;
    snprintf(out.status_message, sizeof(out.status_message), "Hit for %d dmg (x%.2f)", out.damage_dealt, type_multiplier);
    return out;
}