#include <iostream>
#include <string>
#include "card.h"
using namespace std;

string KPossibilities[25] = {
    "BLUE", "BLUE", "BLUE", "BLUE", "BLUE", "BLUE", "BLUE", "BLUE", 
    "RED", "RED", "RED", "RED", "RED", "RED", "RED", "RED", 
    "BLACK", 
    "BEIGE", "BEIGE", "BEIGE", "BEIGE", "BEIGE", "BEIGE", "BEIGE", "BEIGE"
};

class Grid {
    public: 
        Card lineup[25];

        void assignCharacters() {
            //Initializes 25 Cards in lineup with random characters, using the method init().
        }

        void assignKeyCards() {
            /*Creates a copy of KPossibilities.
            Sets the keyCard attribute of each Card in the lineup to a random choice of KCPossibilities.
            Everytime a key is assign, that same key is removed from the copy of KPossibilites.
            At the end of the loop -- once every card has its key, the copy is deleted.
            */
        }
    
};