#pragma once
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>
#include "player.h"
using namespace std;

// A (team, role) seat that a player can take.
typedef pair<Team, Role> Slot;

// First step of the game: players join and pick a team and a role.
// No input/output here, so any interface (console, web, ...) can drive it:
//   openSlots()  -> what can still be picked
//   addPlayer()  -> register a choice (throws invalid_argument if it is not allowed)
//   isFull()     -> every seat is taken, the game can start
// The default rules are 1 SPY and 1 GUESSER per team (4 players); raise the numbers for bigger games.
class Lobby {
    private:
        int spiesPerTeam;
        int guessersPerTeam;
        vector<Player> joined;

        int seatsFor(Role role) const {
            return role == Role::SPY ? spiesPerTeam : guessersPerTeam;
        }

        int takenSeats(Team team, Role role) const {
            int count = 0;
            for (const Player& player : joined) {
                if (player.team == team && player.role == role) count++;
            }
            return count;
        }

    public:
        Lobby(int spiesPerTeam = 1, int guessersPerTeam = 1) {
            this->spiesPerTeam = spiesPerTeam;
            this->guessersPerTeam = guessersPerTeam;
        }

        int totalSeats() const {
            return 2 * (spiesPerTeam + guessersPerTeam);
        }

        bool isOpen(Team team, Role role) const {
            return takenSeats(team, role) < seatsFor(role);
        }

        // Every (team, role) that still has a free seat. A seat that exists twice shows up twice.
        vector<Slot> openSlots() const {
            vector<Slot> slots;
            for (Team team : {Team::BLUE, Team::RED}) {
                for (Role role : {Role::SPY, Role::GUESSER}) {
                    int free = seatsFor(role) - takenSeats(team, role);
                    for (int i = 0; i < free; i++) slots.push_back(Slot(team, role));
                }
            }
            return slots;
        }

        void addPlayer(string name, Team team, Role role) {
            if (name.empty()) {
                throw invalid_argument("Player name cannot be empty");
            }
            for (const Player& player : joined) {
                if (player.name == name) {
                    throw invalid_argument("There is already a player named " + name);
                }
            }
            if (!isOpen(team, role)) {
                throw invalid_argument("No " + toString(role) + " seat left on team " + toString(team));
            }
            joined.push_back(Player(name, team, role));
        }

        // Same as above, for interfaces that send the choices as text (e.g. JSON from a front-end).
        void addPlayer(string name, const string& team, const string& role) {
            addPlayer(name, teamFromString(team), roleFromString(role));
        }

        bool isFull() const {
            return (int)joined.size() == totalSeats();
        }

        const vector<Player>& players() const {
            return joined;
        }
};
