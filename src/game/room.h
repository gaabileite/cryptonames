#pragma once
#include <string>
#include <map>
#include <set>
#include <memory>
#include <chrono>
#include <optional>
#include <random>
#include <stdexcept>
#include <cstdio>
#include "lobby.h"
#include "match.h"
using namespace std;

// Every character there is, and how many each universe has (only the universes that have any).
// All the rooms of a server share one Catalog.
struct Catalog {
    static constexpr size_t cardsOnBoard = 25;       // a game needs this many characters to fill the board

    vector<Character> characters;
    map<M_Universe, int> universeSizes;

    explicit Catalog(vector<Character> all) : characters(std::move(all)) {
        for (const Character& character : characters) universeSizes[character.universe]++;
    }

    int charactersIn(const set<M_Universe>& universes) const {
        int count = 0;
        for (M_Universe universe : universes) {
            auto size = universeSizes.find(universe);
            if (size != universeSizes.end()) count += size->second;
        }
        return count;
    }
};

// The token is not from any player of this room (or of any room).
struct NotInRoom : runtime_error {
    using runtime_error::runtime_error;
};

// The action does not fit the stage the room is in: for example, picking a seat after the match has started.
struct WrongStage : runtime_error {
    using runtime_error::runtime_error;
};

// One room: a waiting room (Lobby) that turns into a Match, and the players that are in it.
// A player enters with join(), which gives a secret token that identifies them from then on.
// No input/output here, so any interface can drive it. Not thread safe: whoever shares a Room between threads locks it.
// Anything not allowed throws invalid_argument (a rule), WrongStage (not at this moment) or NotInRoom (unknown token).
class Room {
    public:
        typedef Match::Clock Clock;

    private:
        string roomCode;
        const Catalog& catalog;
        Lobby lobby;
        unique_ptr<Match> currentMatch;       // created when every seat is taken and everybody is ready
        map<string, string> nameOfToken;      // the secret token of each player -> the player's name
        Clock::time_point lastActivity;

        static string newToken() {
            random_device device;
            char token[33];
            snprintf(token, sizeof(token), "%08x%08x%08x%08x", device(), device(), device(), device());
            return token;
        }

        // The waiting room is closed once the match has started.
        void requireWaitingRoom() const {
            if (currentMatch) {
                throw WrongStage("The game has already started");
            }
        }

    public:
        Room(const string& code, const Catalog& catalog, Clock::time_point now = Clock::now()) : roomCode(code), catalog(catalog), lastActivity(now) {
            set<M_Universe> all;                  // every universe that has characters starts selected
            for (const auto& universe : catalog.universeSizes) all.insert(universe.first);
            lobby.chooseUniverses(all);
        }

        const string& code() const {
            return roomCode;
        }

        // ---- who is in the room

        // Enters the waiting room and returns the player's secret token.
        string join(const string& name) {
            requireWaitingRoom();
            lobby.join(name);                     // throws if the name is not allowed or the room is full
            string token = newToken();
            nameOfToken[token] = lobby.members().back().name;      // the lobby may have trimmed the name
            return token;
        }

        // The player with that token. Throws NotInRoom if there is none.
        const Member& member(const string& token) const {
            auto name = nameOfToken.find(token);
            if (name != nameOfToken.end()) {
                for (const Member& member : lobby.members()) {
                    if (member.name == name->second) return member;
                }
            }
            throw NotInRoom("Unknown or missing token. Join the game first");
        }

        // Leaves the room, at any stage. Leaving a match that is still going on makes the player's team forfeit.
        void leave(const string& token) {
            string name = member(token).name;
            if (currentMatch && !currentMatch->isFinished()) {
                currentMatch->forfeit(name);
            }
            lobby.leave(name);
            for (auto entry = nameOfToken.begin(); entry != nameOfToken.end();) {
                entry = entry->second == name ? nameOfToken.erase(entry) : next(entry);
            }
        }

        // A room without players has no reason to exist.
        bool isEmpty() const {
            return lobby.members().empty();
        }

        // ---- the waiting room

        void chooseSeat(const string& token, Team team, Role role) {
            requireWaitingRoom();
            lobby.chooseSeat(member(token).name, team, role);
        }

        // Marks the player as ready, or not. The match starts by itself when every seat is taken and everybody is ready.
        void setReady(const string& token, bool ready) {
            requireWaitingRoom();
            lobby.setReady(member(token).name, ready);
            if (lobby.canStart()) {
                currentMatch = make_unique<Match>(lobby, catalog.characters);
            }
        }

        // Switches one universe on or off. At least 25 characters must stay selected.
        void setUniverse(const string& token, M_Universe universe, bool selected) {
            requireWaitingRoom();
            member(token);
            if (!catalog.universeSizes.count(universe)) {
                throw invalid_argument("There are no characters in " + toString(universe));
            }

            // One universe at a time, applied to what the room has now, so two players clicking together do not undo each other.
            set<M_Universe> chosen = lobby.universes();
            if (selected) chosen.insert(universe); else chosen.erase(universe);
            if (chosen.empty()) {
                throw invalid_argument("Pick at least one universe");
            }
            int available = catalog.charactersIn(chosen);
            if (available < (int)Catalog::cardsOnBoard) {
                throw invalid_argument("Those universes only have " + to_string(available) + " characters, and a game needs at least " + to_string(Catalog::cardsOnBoard));
            }
            lobby.chooseUniverses(chosen);        // cancels everybody's ready if the choice changed
        }

        // Sets the time limit of the clue or of the guessing. The seconds are kept as they were when none are given.
        // They come as a long long so that a huge number cannot wrap around into a valid one.
        void setTimer(const string& token, TimerPhase phase, bool enabled, optional<long long> seconds) {
            requireWaitingRoom();
            member(token);
            int chosen = lobby.timer(phase).seconds;
            if (seconds) {
                if (*seconds < Lobby::minTurnSeconds || *seconds > Lobby::maxTurnSeconds) {
                    throw invalid_argument("The time can be from " + to_string(Lobby::minTurnSeconds) + " to " + to_string(Lobby::maxTurnSeconds) + " seconds");
                }
                chosen = (int)*seconds;
            }
            lobby.chooseTimer(phase, enabled, chosen);   // cancels everybody's ready if the setting changed
        }

        const Lobby& waitingRoom() const {
            return lobby;
        }

        const Catalog& characters() const {
            return catalog;
        }

        // ---- the match

        bool started() const {
            return currentMatch != nullptr;
        }

        // The match, with its clock brought up to date: when the time of a phase has run out, the turn has already passed.
        Match& match() {
            if (!currentMatch) {
                throw WrongStage("The game has not started yet");
            }
            currentMatch->advanceClock();
            return *currentMatch;
        }

        // ---- activity, to forget rooms that nobody uses any more

        void touch(Clock::time_point now) {
            lastActivity = now;
        }

        Clock::time_point lastUsed() const {
            return lastActivity;
        }
};
