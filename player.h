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

        Player(string name, Team team, Role role) {
            this->name = name;
            this->team = team;
            this->role = role;
        }

        bool isSpy() const {
            return role == Role::SPY;
        }
};
