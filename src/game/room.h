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

// Somebody tried to enter a room while a match is going on without saying what they want to do about it. It says
// what kind of game it is, so the newcomer knows what joining would mean.
struct MatchInProgress : WrongStage {
    bool cooperative;         // everybody plays on the same team, so a newcomer only picks a role
    int players;              // how many are playing
    MatchInProgress(const string& message, bool cooperative, int players)
        : WrongStage(message), cooperative(cooperative), players(players) {}
};

// What somebody who enters a room with a match in progress chooses: play in it (they pick a seat), or stay in the
// room's waiting room until the match is over. UNDECIDED makes join() refuse and say that a match is going on.
enum class JoinChoice { UNDECIDED, PLAY, WAIT };

// One room: a waiting room (Lobby) that turns into a Match, and the players that are in it.
// A player enters with join(), which gives a secret token that identifies them from then on. That is also possible
// while the match is going on, but only after they say what they want (JoinChoice): to play in it, and then they pick
// a seat once (chooseSeat) and play like the others; or to wait in the room's waiting room for the next game. They can
// change their mind with setWaiting() until the match is over. When it is over, anybody in the room can call newGame(),
// and everybody goes back to the waiting room (no seats, nobody ready) to start another match.
// No input/output here, so any interface can drive it. Not thread safe: whoever shares a Room between threads locks it.
// Anything not allowed throws invalid_argument (a rule), WrongStage (not at this moment) or NotInRoom (unknown token).
class Room {
    public:
        typedef Match::Clock Clock;

    private:
        string roomCode;
        const Catalog& catalog;
        Lobby lobby;
        unique_ptr<Match> currentMatch;       // created when the players are ready and nothing is missing
        map<string, string> nameOfToken;      // the secret token of each player -> the player's name
        Clock::time_point lastActivity;
        int gamesStarted = 0;                 // how many matches this room has had

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

        // Enters the room and returns the player's secret token. Before the start the player is in the waiting room.
        // During a match they must say what they want (throws MatchInProgress otherwise): PLAY leaves them without a
        // seat until they pick one, WAIT keeps them in the waiting room. Once the match is over there is nothing to join,
        // so a newcomer just waits for the next game.
        string join(const string& name, JoinChoice choice = JoinChoice::UNDECIDED) {
            bool waiting = false;
            if (currentMatch) {
                if (currentMatch->isFinished()) {
                    waiting = true;
                } else if (choice == JoinChoice::UNDECIDED) {
                    throw MatchInProgress("A game is already in progress in this room", currentMatch->isCooperative(), currentMatch->playing());
                } else {
                    waiting = choice == JoinChoice::WAIT;
                }
            }
            lobby.join(name, waiting);            // throws if the name is not allowed or the room is full
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

        // Leaves the room, at any stage. Leaving a match that is still going on makes the player's team forfeit, unless it
        // still has a spy and a guesser without them.
        void leave(const string& token) {
            const Member& leaving = member(token);
            string name = leaving.name;
            if (currentMatch && leaving.seat) {           // somebody who never took a seat was not playing
                currentMatch->playerLeft(name);
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

        // In the waiting room the seat can be changed until the player is ready. A player who came in during the match
        // and chose to play has no seat yet and takes one, once: it is theirs until the end.
        void chooseSeat(const string& token, Team team, Role role) {
            const Member& chooser = member(token);
            if (!currentMatch) {
                lobby.chooseSeat(chooser.name, team, role);
                return;
            }
            if (currentMatch->isFinished()) {
                throw WrongStage("The match is over");
            }
            if (chooser.waiting) {
                throw WrongStage("You are waiting for the next game. Join the game in progress first");
            }
            if (chooser.seat) {
                throw WrongStage("The game has already started, so your team and role cannot change");
            }
            Player seat = currentMatch->addPlayer(chooser.name, team, role);
            lobby.chooseSeat(chooser.name, seat.team, seat.role);      // the same seat as the match gave (co-operative: BLUE)
        }

        // During a match, somebody without a seat decides to wait for the next game (true) or to join this one (false).
        // Not possible for somebody who already has a seat, nor once the match is over.
        void setWaiting(const string& token, bool waiting) {
            const Member& who = member(token);
            if (!currentMatch) {
                throw WrongStage("There is no game in progress: you are already in the waiting room");
            }
            if (currentMatch->isFinished()) {
                throw WrongStage("The match is over");
            }
            if (who.seat) {
                throw WrongStage("You are already playing in this game");
            }
            lobby.setWaiting(who.name, waiting);
        }

        // After a match: everybody in the room goes back to the waiting room, with no seats and nobody ready (the
        // universes and the time limits stay as they were), to start another match. Anybody in the room can do it.
        void newGame(const string& token) {
            member(token);
            if (!currentMatch || !currentMatch->isFinished()) {
                throw WrongStage("The game is not over yet");
            }
            currentMatch.reset();
            lobby.startOver();
        }

        // How many matches this room has started: tells one match from the next.
        int gameNumber() const {
            return gamesStarted;
        }

        // Marks the player as ready, or not. The match starts by itself when everybody is ready and nothing is missing.
        void setReady(const string& token, bool ready) {
            requireWaitingRoom();
            lobby.setReady(member(token).name, ready);
            if (lobby.canStart()) {
                currentMatch = make_unique<Match>(lobby, catalog.characters);
                gamesStarted++;
                lobby.reseat(currentMatch->players());      // a co-operative game decides the teams and roles itself
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
