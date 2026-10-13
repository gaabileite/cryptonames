#pragma once
#include <iostream>
#include <string>
#include "enums.h"
using namespace std;

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
