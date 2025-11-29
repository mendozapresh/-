// damage_calc.c
// Full implementation: CSV loader, ability->move conversion, type chart, damage formula.

#include "damage_calc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// ---- Type names & chart (canonical order) ----
// Order: Bug, Dark, Dragon, Electric, Fairy, Fighting, Fire,
//        Flying, Ghost, Grass, Ground, Ice, Normal, Poison,
//        Psychic, Rock, Steel, Water
static const char *TYPE_NAMES[] = {
    "Bug", "Dark", "Dragon", "Electric", "Fairy", "Fighting", "Fire",
    "Flying", "Ghost", "Grass", "Ground", "Ice", "Normal", "Poison",
    "Psychic", "Rock", "Steel", "Water"};
#define TYPE_COUNT (sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0]))

// Simple type chart (attacker row -> defender column).
// For brevity below I've included a commonly used chart subset.
// You can adjust values if you want an alternate chart.
// (Values: 0, 0.5, 1.0, 2.0)
static const float TYPE_CHART[TYPE_COUNT][TYPE_COUNT] = {
    // Attacker: Bug
    /*Bug*/ {1, 1, 0.5, 1, 1, 0.5, 0.5, 2, 0.5, 2, 1, 1, 1, 0.5, 2, 1, 1, 1},
    // Dark
    /*Dark*/ {1, 0.5, 1, 1, 0.5, 2, 1, 1, 0.5, 1, 1, 1, 1, 1, 2, 1, 1, 1},
    // Dragon
    /*Dragon*/ {1, 1, 2, 1, 0.5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.5, 0.5},
    // Electric
    /*Electric*/ {1, 1, 0.5, 0.5, 1, 1, 1, 2, 1, 0.5, 0, 1, 1, 1, 1, 1, 1, 2},
    // Fairy
    /*Fairy*/ {1, 2, 2, 1, 1, 0.5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.5, 1},
    // Fighting
    /*Fight*/ {0.5, 0.5, 1, 1, 0.5, 1, 1, 0.5, 1, 1, 1, 2, 1, 0.5, 0.5, 2, 2, 1},
    // Fire
    /*Fire*/ {2, 1, 0.5, 1, 1, 1, 0.5, 1, 1, 2, 1, 2, 1, 1, 1, 0.5, 2, 1},
    // Flying
    /*Flying*/ {0.5, 1, 1, 0.5, 1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 0.5, 1, 1},
    // Ghost
    /*Ghost*/ {1, 2, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 0, 1, 1, 1, 1, 1},
    // Grass
    /*Grass*/ {0.5, 1, 0.5, 1, 1, 1, 0.5, 0.5, 1, 0.5, 2, 1, 1, 0.5, 1, 2, 1, 2},
    // Ground
    /*Ground*/ {1, 1, 1, 2, 1, 1, 2, 0, 1, 0.5, 1, 2, 1, 2, 1, 0.5, 2, 1},
    // Ice
    /*Ice*/ {1, 1, 2, 1, 1, 1, 0.5, 2, 1, 2, 2, 0.5, 1, 1, 1, 1, 0.5, 1},
    // Normal
    /*Normal*/ {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0.5, 1, 1},
    // Poison
    /*Poison*/ {0.5, 1, 1, 1, 0.5, 1, 1, 1, 1, 2, 1, 1, 1, 0.5, 1, 1, 1, 1},
    // Psychic
    /*Psychic*/ {1, 0, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 0.5, 1, 0.5, 1, 1, 1},
    // Rock
    /*Rock*/ {2, 1, 1, 1, 1, 0.5, 2, 2, 1, 1, 0.5, 2, 1, 1, 1, 0.5, 1, 1},
    // Steel
    /*Steel*/ {1, 1, 1, 0.5, 1, 1, 0.5, 1, 1, 1, 1, 2, 1, 1, 1, 2, 0.5, 0.5},
    // Water
    /*Water*/ {1, 1, 0.5, 0.5, 1, 1, 0.5, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 0.5}};

// ---- Globals (exported in header) ----
PokemonData POKEMON_DB[MAX_POKEMON];
int POKEMON_COUNT = 0;
MoveData MOVE_DB[MAX_MOVES];
int MOVE_COUNT = 0;

// ------------------------------------------------------------------
// Windows compatibility: case-insensitive strstr if needed
// ------------------------------------------------------------------
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

// ------------------------------------------------------------------
// CSV parsing helpers (quoted fields safe)
// ------------------------------------------------------------------
static char *find_nth_field(char *line, int n)
{
    char *p = line;
    for (int i = 0; i < n; ++i)
    {
        if (*p == '\0')
            return NULL;

        if (*p == '"')
        {
            // Skip quoted field until matching closing quote
            p++;
            while (*p && *p != '"')
                p++;
            if (!*p)
                return NULL;
            p++; // skip closing quote
        }

        // find comma separator
        p = strchr(p, ',');
        if (!p)
            return NULL;
        p++; // move into next field
    }
    return p;
}

static void extract_field(char *src, char *dest, size_t max_len)
{
    if (!src || !dest || max_len == 0)
        return;
    char *end = src;
    while (*end != '\0' && *end != ',' && *end != '\n' && *end != '\r')
        end++;
    size_t len = (size_t)(end - src);
    if (len >= max_len)
        len = max_len - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';

    // Trim surrounding quotes and whitespace
    char *s = dest;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == '"' && s[1] != '\0')
        s++;
    char *e = dest + strlen(dest) - 1;
    while (e >= s && isspace((unsigned char)*e))
    {
        *e = '\0';
        e--;
    }
    if (e >= s && *e == '"')
        *e = '\0';

    if (s != dest)
        memmove(dest, s, strlen(s) + 1);
}

// ------------------------------------------------------------------
// Open CSV file helper
// ------------------------------------------------------------------
FILE *open_pokemon_csv(const char *path)
{
    if (!path)
        return NULL;
    FILE *f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "[ERROR: damage_calc] Could not open %s\n", path);
        return NULL;
    }
    return f;
}

// ------------------------------------------------------------------
// Parse abilities string into array
// Accepts formats like:
//  "['Overgrow','Chlorophyll']" or "Overgrow" or "['Blaze', 'Solar Power']"
// Returns number of parsed abilities
// ------------------------------------------------------------------
int parse_abilities(const char *field, char abilities[][MAX_MOVE_NAME], int max_abilities)
{
    if (!field || !abilities || max_abilities <= 0)
        return 0;
    char buf[1024];
    size_t L = strlen(field);
    if (L >= sizeof(buf))
        L = sizeof(buf) - 1;
    memcpy(buf, field, L);
    buf[L] = '\0';

    int count = 0;

    // If single quotes are present, extract segments between them
    if (strchr(buf, '\''))
    {
        char *p = buf;
        while (count < max_abilities)
        {
            char *start = strchr(p, '\'');
            if (!start)
                break;
            start++;
            char *end = strchr(start, '\'');
            if (!end)
                break;
            int len = (int)(end - start);
            if (len > 0)
            {
                if (len >= MAX_MOVE_NAME)
                    len = MAX_MOVE_NAME - 1;
                memcpy(abilities[count], start, len);
                abilities[count][len] = '\0';
                // trim
                char *s = abilities[count];
                while (*s && isspace((unsigned char)*s))
                    s++;
                if (s != abilities[count])
                    memmove(abilities[count], s, strlen(s) + 1);
                count++;
            }
            p = end + 1;
        }
        return count;
    }

    // Fallback: split by comma
    char *token;
    char tmp[1024];
    strncpy(tmp, buf, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    token = strtok(tmp, ",");
    while (token && count < max_abilities)
    {
        // trim whitespace and quotes
        while (*token && isspace((unsigned char)*token))
            token++;
        char *tend = token + strlen(token) - 1;
        while (tend >= token && isspace((unsigned char)*tend))
        {
            *tend = '\0';
            tend--;
        }
        if (*token == '"' || *token == '\'')
        {
            size_t tlen = strlen(token);
            if (tlen > 1 && (token[tlen - 1] == '"' || token[tlen - 1] == '\''))
            {
                token[tlen - 1] = '\0';
                token++;
            }
        }
        if (*token)
        {
            strncpy(abilities[count], token, MAX_MOVE_NAME - 1);
            abilities[count][MAX_MOVE_NAME - 1] = '\0';
            count++;
        }
        token = strtok(NULL, ",");
    }
    return count;
}

// ------------------------------------------------------------------
// Helper: map type name -> index
// ------------------------------------------------------------------
static int type_name_to_index(const char *t)
{
    if (!t)
        return -1;
    for (int i = 0; i < (int)TYPE_COUNT; ++i)
    {
        if (strcasecmp(t, TYPE_NAMES[i]) == 0)
            return i;
    }
    return -1;
}

// ------------------------------------------------------------------
// load_pokemon_data: parses CSV and populates POKEMON_DB
// Column indices based on user's provided CSV layout (0-indexed):
// abilities (0), against_* columns (1..18), attack(19), base_egg_steps(20)...
// ...hp is at 28, name at 30, sp_attack 33, sp_defense 34, speed 35, type1 36, type2 37
// ------------------------------------------------------------------
void load_pokemon_data(const char *csv_path)
{
    FILE *file = open_pokemon_csv(csv_path);
    if (!file)
    {
        // fallback small dataset (three starters)
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
#ifdef MAX_ABILITIES_PER_POKEMON
        p.ability_count = 0;
#endif
        POKEMON_DB[POKEMON_COUNT++] = p;
        return;
    }

// indices (according to your description)
#define IDX_ABILITIES 0
#define IDX_AGAINST_START 1 // against_bug .. against_water (18 columns)
#define IDX_ATTACK 19
#define IDX_DEFENSE 25
#define IDX_HP 28
#define IDX_NAME 30
#define IDX_SP_ATTACK 33
#define IDX_SP_DEFENSE 34
#define IDX_SPEED 35
#define IDX_TYPE1 36
#define IDX_TYPE2 37

    char line[8192];
    char tmp[1024];
    POKEMON_COUNT = 0;

    // skip header
    if (!fgets(line, sizeof(line), file))
    {
        fclose(file);
        return;
    }

    while (fgets(line, sizeof(line), file) && POKEMON_COUNT < MAX_POKEMON)
    {
        PokemonData *p = &POKEMON_DB[POKEMON_COUNT];
        memset(p, 0, sizeof(PokemonData));

        // working copy for find_nth_field because it walks the pointer
        char copy[8192];
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';

        // abilities
        char *fld = find_nth_field(copy, IDX_ABILITIES);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
#ifdef MAX_ABILITIES_PER_POKEMON
            p->ability_count = parse_abilities(tmp, p->abilities, MAX_ABILITIES_PER_POKEMON);
#endif
        }
        else
        {
#ifdef MAX_ABILITIES_PER_POKEMON
            p->ability_count = 0;
#endif
        }

        // against_* columns: store into p->against[] if present in struct
        for (int t = 0; t < TYPE_COUNT; ++t)
        {
            strncpy(copy, line, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            fld = find_nth_field(copy, IDX_AGAINST_START + t);
            if (fld)
            {
                extract_field(fld, tmp, sizeof(tmp));
                float v = 1.0f;
                if (tmp[0] != '\0')
                    v = (float)atof(tmp);
#ifdef HAVE_POKEMON_AGAINST_ARRAY
                p->against[t] = v;
#endif
            }
            else
            {
#ifdef HAVE_POKEMON_AGAINST_ARRAY
                p->against[t] = 1.0f;
#endif
            }
        }

        // attack
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_ATTACK);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->attack = tmp[0] ? atoi(tmp) : 0;
        }

        // defense
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_DEFENSE);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->defense = tmp[0] ? atoi(tmp) : 1;
        }

        // hp
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_HP);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->hp = tmp[0] ? atoi(tmp) : 1;
        }

        // name
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_NAME);
        if (fld)
        {
            extract_field(fld, p->name, sizeof(p->name));
        }
        if (p->name[0] == '\0')
            continue; // skip blank rows

        // sp_attack
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_SP_ATTACK);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->sp_attack = tmp[0] ? atoi(tmp) : 0;
        }

        // sp_defense
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_SP_DEFENSE);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->sp_defense = tmp[0] ? atoi(tmp) : 1;
        }

        // speed
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_SPEED);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            p->speed = tmp[0] ? atoi(tmp) : 0;
        }

        // type1
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_TYPE1);
        if (fld)
            extract_field(fld, p->type1, sizeof(p->type1));

        // type2 (may be empty)
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        fld = find_nth_field(copy, IDX_TYPE2);
        if (fld)
            extract_field(fld, p->type2, sizeof(p->type2));
        if (p->type2[0] == '\0')
            strncpy(p->type2, "NONE", sizeof(p->type2) - 1);

        POKEMON_COUNT++;
    }

    fclose(file);

#undef IDX_ABILITIES
#undef IDX_AGAINST_START
#undef IDX_ATTACK
#undef IDX_DEFENSE
#undef IDX_HP
#undef IDX_NAME
#undef IDX_SP_ATTACK
#undef IDX_SP_DEFENSE
#undef IDX_SPEED
#undef IDX_TYPE1
#undef IDX_TYPE2
}

// ------------------------------------------------------------------
// Build moves from Pokemon abilities
// Rules:
//  - Move.name = ability name
//  - Move.type = Pokemon.type1
//  - Move.category = MOVE_SPECIAL
//  - Move.power = Pokemon.attack (fallback 50)
// ------------------------------------------------------------------
void load_moves_from_pokemon()
{
    MOVE_COUNT = 0;
    for (int i = 0; i < POKEMON_COUNT && MOVE_COUNT < MAX_MOVES; ++i)
    {
        PokemonData *p = &POKEMON_DB[i];
#ifdef MAX_ABILITIES_PER_POKEMON
        for (int a = 0; a < p->ability_count && MOVE_COUNT < MAX_MOVES; ++a)
        {
            MoveData *m = &MOVE_DB[MOVE_COUNT];
            memset(m, 0, sizeof(MoveData));
            strncpy(m->name, p->abilities[a], MAX_MOVE_NAME - 1);
            if (p->type1[0] != '\0')
                strncpy(m->type, p->type1, MAX_TYPE_NAME - 1);
            else
                strncpy(m->type, "Normal", MAX_TYPE_NAME - 1);
            m->category = MOVE_SPECIAL;
            m->power = (p->attack > 0) ? p->attack : 50;
            MOVE_COUNT++;
        }
#endif
    }
}

// ------------------------------------------------------------------
// Convenience loader: load pokemon CSV and generate moves
// ------------------------------------------------------------------
void load_all_pokemon_and_moves(const char *pokemon_csv)
{
    load_pokemon_data(pokemon_csv);
    load_moves_from_pokemon();
}

// ------------------------------------------------------------------
// load_moves_csv (existing function) - robust CSV loader for moves
// If you also have a real moves.csv, this will populate MOVE_DB from it.
// ------------------------------------------------------------------
void load_moves_csv(const char *csv_path)
{
    FILE *file = fopen(csv_path, "r");
    if (!file)
    {
        fprintf(stderr, "[ERROR: damage_calc] Could not open %s. Using generated moves.\n", csv_path);
        return;
    }

    MOVE_COUNT = 0;
    char line[1024];
    char tmp[128];

    // skip header
    if (!fgets(line, sizeof(line), file))
    {
        fclose(file);
        return;
    }

    while (fgets(line, sizeof(line), file) && MOVE_COUNT < MAX_MOVES)
    {
        MoveData *m = &MOVE_DB[MOVE_COUNT];
        memset(m, 0, sizeof(MoveData));

        char copy[1024];
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';

        // simple CSV fields: name,type,category,power (0..)
        // Name (index 0)
        char *fld = find_nth_field(copy, 0);
        if (fld)
        {
            extract_field(fld, m->name, sizeof(m->name));
        }
        if (m->name[0] == '\0')
            continue;

        // Type (1)
        strncpy(copy, line, sizeof(copy) - 1);
        fld = find_nth_field(copy, 1);
        if (fld)
            extract_field(fld, m->type, sizeof(m->type));

        // Category (2)
        strncpy(copy, line, sizeof(copy) - 1);
        fld = find_nth_field(copy, 2);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            if (strcasecmp(tmp, "Physical") == 0)
                m->category = MOVE_PHYSICAL;
            else if (strcasecmp(tmp, "Special") == 0)
                m->category = MOVE_SPECIAL;
            else
                m->category = MOVE_STATUS;
        }

        // Power (3)
        strncpy(copy, line, sizeof(copy) - 1);
        fld = find_nth_field(copy, 3);
        if (fld)
        {
            extract_field(fld, tmp, sizeof(tmp));
            m->power = tmp[0] ? atoi(tmp) : 0;
        }

        MOVE_COUNT++;
    }

    fclose(file);
}

// ------------------------------------------------------------------
// Lookup helpers
// ------------------------------------------------------------------
const PokemonData *get_pokemon(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < POKEMON_COUNT; ++i)
    {
        if (strcasecmp(POKEMON_DB[i].name, name) == 0)
            return &POKEMON_DB[i];
    }
    return NULL;
}

const MoveData *get_move(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < MOVE_COUNT; ++i)
    {
        if (strcasecmp(MOVE_DB[i].name, name) == 0)
            return &MOVE_DB[i];
    }
    return NULL;
}

// ------------------------------------------------------------------
// Apply a boost stage to a base stat. Stages follow Pokémon conventions:
// Stage -> multiplier (simplified):
// -6:-2/8, -5:-1/3, -4:-1/2, -3:-1/1.5, -2:-2/3, -1:2/3, 0:1, +1:3/2, +2:2, +3:2.5, ...
// For simplicity we use a table up to +/-6
// ------------------------------------------------------------------
int apply_boost(int base_stat, int boost_stage)
{
    static const float STAGE_MULTS[] = {0.25f, 0.2857f, 0.3333f, 0.4f, 0.5f, 0.6667f, 1.0f,
                                        1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f};
    // index 6 -> stage 0
    int idx = boost_stage + 6;
    if (idx < 0)
        idx = 0;
    if (idx > 12)
        idx = 12;
    float mult = STAGE_MULTS[idx];
    return (int)floorf(base_stat * mult + 0.0001f);
}

// ------------------------------------------------------------------
// Get type multiplier using TYPE_CHART (attack_type -> defender_type)
// ------------------------------------------------------------------
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
    if (total <= 0.0f)
        total = 1.0f;
    return total;
}

// ------------------------------------------------------------------
// Core: calculate_damage_logic per spec
// - AttackerStat: attack or sp_attack depending on move category
// - DefenderStat: defense or sp_defense depending on move category
// - BasePower: move->power (default 1.0 if 0)
// - Type multipliers: product of type1 and type2 effectiveness
// - Returns DamageResult with damage_dealt and defender_remaining_hp
// ------------------------------------------------------------------
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
        out.damage_dealt = 0;
        out.defender_remaining_hp = D ? D->hp : 100;
        snprintf(out.status_message, sizeof(out.status_message), "Missed (Invalid Data)");
        return out;
    }

    // choose attacker/defender stat
    float attacker_stat = 1.0f;
    float defender_stat = 1.0f;
    if (M->category == MOVE_PHYSICAL)
    {
        attacker_stat = (float)A->attack;
        defender_stat = (float)D->defense;
    }
    else if (M->category == MOVE_SPECIAL)
    {
        attacker_stat = (float)A->sp_attack;
        defender_stat = (float)D->sp_defense;
    }
    else
    {
        // status moves do no direct damage
        out.damage_dealt = 0;
        out.defender_remaining_hp = D->hp;
        snprintf(out.status_message, sizeof(out.status_message), "No damage (Status move)");
        return out;
    }

    if (defender_stat <= 0.0f)
        defender_stat = 1.0f;

    float base_power = (M->power > 0) ? (float)M->power : 1.0f;

    // Type multipliers using defender's type1 and type2
    float type_multiplier = get_type_multiplier(M->type, D->type1, D->type2);

    // Final damage formula per spec
    float raw_damage = base_power * (attacker_stat / defender_stat) * type_multiplier;

    // Floor to int, ensure at least 1
    int damage = (int)floorf(raw_damage + 0.00001f);
    if (damage < 1)
        damage = 1;

    out.damage_dealt = damage;
    out.defender_remaining_hp = D->hp - damage;
    if (out.defender_remaining_hp < 0)
        out.defender_remaining_hp = 0;

    snprintf(out.status_message, sizeof(out.status_message), "Hit for %d dmg (x%.2f)", out.damage_dealt, type_multiplier);
    return out;
}