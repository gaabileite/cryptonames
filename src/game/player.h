#pragma once
#include <iostream>
#include <string>
#include "enums.h"
using namespace std;

class Player {
    public:
        string name;
        Team team;
        Role role;
        bool creator;     // the host: the player who made the room (or, if they left, the one who took their place)

        Player(string name, Team team, Role role, bool creator = false) {
            this->name = name;
            this->team = team;
            this->role = role;
            this->creator = creator;
        }

        bool isSpy() const {
            return role == Role::SPY;
        }
};
