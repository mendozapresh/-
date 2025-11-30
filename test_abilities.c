#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// --- Definitions copied from damage_calc.h ---
#define MAX_MOVE_NAME 32
#define MAX_ABILITIES_PER_POKEMON 8

// ---------------------------------------------

// --- CORRECTED parse_abilities FUNCTION ---
int parse_abilities(const char *field, char abilities[][MAX_MOVE_NAME], int max_abilities)
{
    if (!field || !abilities || max_abilities <= 0)
        return 0;
    int count = 0;
    const char *p = field;

    // 1. Move pointer to the start of the *first* ability name (past '[' and potential '"').
    p = strchr(p, '\''); 
    if (!p) return 0;
    p++; // Now pointing at the first character of the first ability name.

    // 2. Loop until the end of the ability list ']' or max abilities reached.
    while (*p && *p != ']' && count < max_abilities)
    {
        // 3. Find the closing single quote (''') for the current ability name.
        const char *end_q = strchr(p, '\'');

        if (end_q) {
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
            while (*p && (*p == '\'' || *p == ',' || *p == ' '))
                p++;
            
        } else {
            // Malformed string (missing closing quotes).
            break; 
        }
    }
    return count;
}
// ------------------------------------------

// --- TEST UTILITY FUNCTION ---
void run_test(const char *input) {
    char abilities[MAX_ABILITIES_PER_POKEMON][MAX_MOVE_NAME];
    int count = parse_abilities(input, abilities, MAX_ABILITIES_PER_POKEMON);

    printf("Input: %s\n", input);
    printf("-> Count: %d\n", count);
    if (count > 0) {
        printf("-> Abilities Found:\n");
        for (int i = 0; i < count; i++) {
            printf("   [%d] \"%s\"\n", i + 1, abilities[i]);
        }
    }
    printf("\n");
}

int main() {
    printf("--- Running parse_abilities Tests ---\n\n");
    
    // Test Case 1: Single-word, multiple abilities (from Bulbasaur)
    run_test("['Overgrow', 'Chlorophyll']");
    
    // Test Case 2: Multi-word, multiple abilities (from Rattata)
    run_test("['Run Away', 'Guts', 'Hustle', 'Gluttony', 'Hustle', 'Thick Fat']");

    // Test Case 3: Mixed (from Pidgey)
    run_test("['Keen Eye', 'Tangled Feet', 'Big Pecks']");
    
    // Test Case 4: Single ability (from Ivysaur)
    run_test("['Overgrow']");

    // Test Case 5: Complex string with irregular spacing/quotes (malformed/unlikely in your CSV, but robust)
    run_test(" [ \"'  Ability A ' , ' Ability B ' , 'AbilityC' ] \" ");
    
    // Test Case 6: Empty list
    run_test("[]");
    
    return 0;
}