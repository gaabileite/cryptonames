#pragma once
#include <iostream>
#include <string>
#include "character.h"
#include "enums.h"
using namespace std;

class Card {
    public:
        string key;
        CardState state;

        Character obj_character;
        string characterName;
        string image_path;

        Card() {
            obj_character = Character();
            characterName = "";
            image_path = "";
            key = "";
            state = CardState::SECRET;
        }

        void init(Character character) {
            obj_character = character;
            characterName = character.name;
            image_path = character.image_path;
            key = "";
            state = CardState::SECRET;
        }

        string turnCard() {
            return key;
        }
};