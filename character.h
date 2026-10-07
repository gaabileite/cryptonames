#pragma once
#include <iostream>
#include <string>
#include <stdexcept>
using namespace std;

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

// The strings below must match the CHECK constraints of the table in characters.db.
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

class Character {
    public:
        string name;
        M_CharacterType type;
        M_Universe universe;
        string image_path;

        Character() {
            name = "";
            type = M_CharacterType::PERSON;
            universe = M_Universe::MCU;
            image_path = "";
        }

        Character(string name, M_CharacterType type, M_Universe universe, string image_path) {
            this->name = name;
            this->type = type;
            this->universe = universe;
            this->image_path = image_path;
        }
};
