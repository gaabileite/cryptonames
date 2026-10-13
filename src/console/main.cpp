#include <iostream>
#include <string>
#include "game/character.h"
#include "game/card.h"
#include "game/grid.h"
#include "game/lobby.h"
#include "game/match.h"
#include "db/database.h"

using namespace std;

// Console version of the player assignment. A front-end replaces this function
// and calls lobby.openSlots() / lobby.addPlayer() itself.
void assignPlayersFromConsole(Lobby& lobby) {
    while (!lobby.isFull()) {
        cout << "\nPlayer " << lobby.players().size() + 1 << " of " << lobby.totalSeats() << endl;

        cout << "Name: ";
        string name;
        if (!getline(cin, name)) {
            throw runtime_error("Input closed before all players joined");
        }

        vector<Slot> slots = lobby.openSlots();
        for (size_t i = 0; i < slots.size(); i++) {
            cout << "  " << i + 1 << ") " << toString(slots[i].first) << " " << toString(slots[i].second) << endl;
        }
        cout << "Pick a team and role: ";
        string choice;
        if (!getline(cin, choice)) {
            throw runtime_error("Input closed before all players joined");
        }

        try {
            int index = -1;
            try {
                index = stoi(choice) - 1;
            } catch (const exception&) {
                // leaves index out of range, handled below
            }
            if (index < 0 || index >= (int)slots.size()) {
                throw invalid_argument("Pick a number from the list");
            }
            lobby.addPlayer(name, slots[index].first, slots[index].second);
        } catch (const invalid_argument& e) {
            cout << "Could not add player: " << e.what() << ". Try again." << endl;
        }
    }
}

// Console version of the board: 5 cards per line, numbered 1-25, with the key shown when it is not hidden.
void printBoard(const vector<CardView>& view) {
    for (int i = 0; i < 25; i++) {
        cout << i + 1 << ". " << view[i].characterName;
        if (view[i].key != CardKey::BEIGE || view[i].state == CardState::REVEALED) {
            cout << " [" << toString(view[i].key) << "]";
        }
        cout << ((i % 5 == 4) ? "\n" : "  |  ");
    }
}

string nameOf(const Match& match, Team team, Role role) {
    for (const Player& player : match.players()) {
        if (player.team == team && player.role == role) return player.name;
    }
    throw logic_error("No such player");
}

// Console version of the turns: the GUESSER of the current team types a card number.
void playFromConsole(Match& match) {
    while (!match.isFinished()) {
        Team team = match.currentTeam();
        string guesser = nameOf(match, team, Role::GUESSER);

        cout << "\nTurn of team " << toString(team) << " - guesser: " << guesser << endl;
        cout << "Cards left: BLUE " << match.remaining(Team::BLUE) << ", RED " << match.remaining(Team::RED) << endl;
        printBoard(match.viewFor(guesser));

        cout << "Card number (1-25), 0 to end the turn, s to see the spy's board: ";
        string input;
        if (!getline(cin, input)) {
            throw runtime_error("Input closed before the match ended");
        }

        try {
            if (input == "s") {
                printBoard(match.viewFor(nameOf(match, team, Role::SPY)));
            } else if (input == "0") {
                match.endTurn(guesser);
            } else {
                int number = -1;
                try {
                    number = stoi(input);
                } catch (const exception&) {
                    // leaves number out of range, handled below
                }
                if (number < 1 || number > 25) {
                    throw invalid_argument("Type a number from 1 to 25");
                }
                CardKey key = match.guess(guesser, number - 1);
                cout << "It was a " << toString(key) << " card." << endl;
            }
        } catch (const invalid_argument& e) {
            cout << "Could not do that: " << e.what() << endl;
        }
    }

    cout << "\nThe match is over. Team " << toString(match.winner()) << " wins!" << endl;
}

int main() {
    try {
        vector<Character> characters = loadCharacters(defaultDatabasePath);
        if (characters.size() < 25) {
            throw runtime_error("The database needs at least 25 characters, found " + to_string(characters.size()));
        }

        Lobby lobby;
        assignPlayersFromConsole(lobby);

        Match match(lobby, characters);
        playFromConsole(match);
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
