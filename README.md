# CSNETWKMP
CSNETWK MP Group 6
Members: 
ANG, Lauren Gabrielle N. ;
BOLADO, Gian Clarence T. ;
ESQUILLO, Faith P. ; 
MENDOZA, Precious Justine B.

*AI was used to debug and create detailed in-line comments for the cross understanding of all members*

Blank stats changed to:
height_m = 1
percentage_male = 50
type 2 = randomized
weight_kg = 5

How to run:
C:\Users\Precious\Downloads\CSNETWKMP-main\CSNETWKMP-main
1. gcc main.c network.c game_logic.c damage_calc.c chat.c -o pokemon.exe -lws2_32 -std=c99
2. pokemon host 8080 (HOST)
3. pokemon join 8081 127.0.0.1 8080 (JOIN)
4. pokemon spectate 8082 127.0.0.1 8080 (SPECTATE)


Documentation:

GAME LOGIC
1. game_logic.h
- game state
- defines the structures that hold the battle data
- BattleState Enum: Defines the valid states defined in the RFC: SETUP, WAITING_FOR_MOVE, PROCESSING_TURN, and GAME_OVER 
- BattleContext Struct: The primary object passed around functions. It stores the true status of the current game (Who is attacking? What move? What is the HP?) 
- GameMessage Struct: A standardized format for messages
2. game_logic.c
- Logic Implementation. It enforces the specific 4-Step Handshake required by the RFC
- process_incoming_message(): The central router. It checks the message_type and dispatches it to the correct handler
- handle_attack_announce(): Validates that the opponent is acting out of turn. If valid, it triggers the automatic DEFENSE_ANNOUNCE response 
- handle_calculation_report(): This is the Discrepancy Resolution engine. It compares the local math result against the opponent's report. If they disagree, it triggers a RESOLUTION_REQUEST instead of confirming the turn 
- finalize_turn(): Handles the end-of-turn logic, including checking for GAME_OVER conditions (HP lower or equal 0) and switching the is_my_turn flag

DAMAGE CALCULATION
1. damage_calc.h
- defines the interface for Pokemon damage calculations, moves, lookups, and multipliers
- PokemonData Struct: Stores a Pokemon's base stats and types
- MoveData Struct: Stores a move's type, category, and power
- DamageResult Struct: Stores the outcome of a damage calculation
2. damage_calc.c
- Implements actual damage calculation, database, and utility functions
- calculate_damage_logic() — Computes deterministic damage based on attacker, defender, and move.
- get_pokemon() — Returns a Pokémon entry by name or partial match.
- get_move() — Returns a move entry by name or partial match.
- load_pokemon_data() — Loads Pokémon stats from CSV; falls back to minimal default set.
- load_moves_csv() — Loads moves from CSV; falls back to default moves.
- get_type_multiplier() — Returns combined type effectiveness multiplier for dual-typed defenders.
- apply_boost() — Adjusts stats based on boost stages
