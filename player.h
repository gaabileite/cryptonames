#pragma once
#include <iostream>
#include <string>
#include <stdexcept>
using namespace std;

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
