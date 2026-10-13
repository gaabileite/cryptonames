#pragma once
#include <string>
#include <stdexcept>
using namespace std;

// Every enum of the game, with its toString() / ...FromString() conversions.

// Character (the strings must match the CHECK constraints of the table in characters.db)
enum class M_CharacterType { PERSON, OBJECT };

enum class M_Universe {
    MCU,
    FOX_XMEN,
    FANTASTIC_FOUR,
    COMICS,
    VENOM,
    SONY_SPIDERMAN,
    THROUGH_THE_SPIDERVERSE
};

inline string toString(M_CharacterType type) {
    switch (type) {
        case M_CharacterType::PERSON: return "Person";
        case M_CharacterType::OBJECT: return "Object";
    }
    throw invalid_argument("Unknown CharacterType");
}

inline string toString(M_Universe universe) {
    switch (universe) {
        case M_Universe::MCU: return "MCU";
        case M_Universe::FOX_XMEN: return "FOX X-MEN";
        case M_Universe::FANTASTIC_FOUR: return "FANTASTIC FOUR";
        case M_Universe::COMICS: return "COMICS";
        case M_Universe::VENOM: return "VENOM";
        case M_Universe::SONY_SPIDERMAN: return "SONY SPIDER-MAN";
        case M_Universe::THROUGH_THE_SPIDERVERSE: return "THROUGH THE SPIDERVERSE";
    }
    throw invalid_argument("Unknown Universe");
}

inline M_CharacterType characterTypeFromString(const string& s) {
    if (s == "Person") return M_CharacterType::PERSON;
    if (s == "Object") return M_CharacterType::OBJECT;
    throw invalid_argument("Unknown character type: " + s);
}

inline M_Universe universeFromString(const string& s) {
    if (s == "MCU") return M_Universe::MCU;
    if (s == "FOX X-MEN") return M_Universe::FOX_XMEN;
    if (s == "FANTASTIC FOUR") return M_Universe::FANTASTIC_FOUR;
    if (s == "COMICS") return M_Universe::COMICS;
    if (s == "VENOM") return M_Universe::VENOM;
    if (s == "SONY SPIDER-MAN") return M_Universe::SONY_SPIDERMAN;
    if (s == "THROUGH THE SPIDERVERSE") return M_Universe::THROUGH_THE_SPIDERVERSE;
    throw invalid_argument("Unknown universe: " + s);
}

// Player
enum class Team { BLUE, RED };

enum class Role { SPY, GUESSER };

inline string toString(Team team) {
    switch (team) {
        case Team::BLUE: return "BLUE";
        case Team::RED: return "RED";
    }
    throw invalid_argument("Unknown Team");
}

inline string toString(Role role) {
    switch (role) {
        case Role::SPY: return "SPY";
        case Role::GUESSER: return "GUESSER";
    }
    throw invalid_argument("Unknown Role");
}

inline Team teamFromString(const string& s) {
    if (s == "BLUE") return Team::BLUE;
    if (s == "RED") return Team::RED;
    throw invalid_argument("Unknown team: " + s);
}

inline Role roleFromString(const string& s) {
    if (s == "SPY") return Role::SPY;
    if (s == "GUESSER") return Role::GUESSER;
    throw invalid_argument("Unknown role: " + s);
}

// Card
enum class CardState { SECRET, REVEALED };

inline string toString(CardState state) {
    switch (state) {
        case CardState::SECRET: return "SECRET";
        case CardState::REVEALED: return "REVEALED";
    }
    throw invalid_argument("Unknown CardState");
}

// The colour a card has for the spies
enum class CardKey { BLUE, RED, BEIGE, BLACK };

inline string toString(CardKey key) {
    switch (key) {
        case CardKey::BLUE: return "BLUE";
        case CardKey::RED: return "RED";
        case CardKey::BEIGE: return "BEIGE";
        case CardKey::BLACK: return "BLACK";
    }
    throw invalid_argument("Unknown CardKey");
}

inline CardKey cardKeyFromString(const string& s) {
    if (s == "BLUE") return CardKey::BLUE;
    if (s == "RED") return CardKey::RED;
    if (s == "BEIGE") return CardKey::BEIGE;
    if (s == "BLACK") return CardKey::BLACK;
    throw invalid_argument("Unknown card key: " + s);
}

// The key of the cards that belong to a team
inline CardKey keyOf(Team team) {
    return team == Team::BLUE ? CardKey::BLUE : CardKey::RED;
}

// Match
enum class MatchState { IN_PROGRESS, FINISHED };

inline string toString(MatchState state) {
    switch (state) {
        case MatchState::IN_PROGRESS: return "IN_PROGRESS";
        case MatchState::FINISHED: return "FINISHED";
    }
    throw invalid_argument("Unknown MatchState");
}
