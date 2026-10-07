#include <iostream>
#include <string>
#include "character.h"
using namespace std;

class Card {
    private:
        string key;

    public:
        Character obj_character;
        string characterName;
        string image_path;

        Card() {
            obj_character = Character();
            characterName = "";
            image_path = "";
            key = "";
        }

        void init(Character character) {
            obj_character = character;
            characterName = character.name;
            image_path = character.image_path;
            key = "";
        }

        string turnCard() {
            return key;
        }
};