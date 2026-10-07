#include <iostream>
#include <string>
#include "character.h"
#include "card.h"
#include "grid.h"
#include "database.h"
#include "lobby.h"

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

int main() {
    try {
        Lobby lobby;
        assignPlayersFromConsole(lobby);

        cout << "\nPlayers:" << endl;
        for (const Player& player : lobby.players()) {
            cout << "  " << player.name << " - " << toString(player.team) << " " << toString(player.role) << endl;
        }

        Grid grid(loadCharacters("characters.db"));
        grid.assignCharacters();
        grid.assignKeyCards();

        cout << "\nGrid (spy view):" << endl;
        for (int i = 0; i < 25; i++) {
            cout << grid.lineup[i].characterName << " [" << grid.lineup[i].key << "]";
            cout << ((i % 5 == 4) ? "\n" : "  |  ");
        }
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
