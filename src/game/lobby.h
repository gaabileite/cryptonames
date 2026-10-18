//Sets up the board and teams.

#pragma once
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <set>
#include <stdexcept>
#include "player.h"
#include "enums.h"
using namespace std;

// A (team, role) seat that a player can take.
typedef pair<Team, Role> Slot;

// A time limit: off, or a number of seconds. There are two in a match, one for the spy to give the clue and one for
// the guesser to pick cards. When the time of a phase runs out, the turn passes to the other team.
struct TimeLimit {
    bool enabled = false;
    int seconds = 60;
};

enum class TimerPhase { CLUE, GUESS };

// Someone in the waiting room. The seat stays empty until they pick a team and a role.
struct Member {
    string name;
    optional<Slot> seat;
    bool ready = false;
};

// First step of the game: the waiting room. No input/output here, so any interface (console, web, ...) can drive it.
//   join(name)                 -> enter the waiting room, without a seat yet (cancels everybody's ready)
//   leave(name)                -> leave it (cancels everybody's ready)
//   chooseSeat(name, team, role) -> pick or change the seat (not while ready)
//   setReady(name, ready)      -> (un)mark yourself as ready; needs a seat
//   chooseUniverses(set)       -> which universes the characters of the match come from (cancels everybody's ready)
//   chooseTimer(phase, on, seconds) -> whether the clue / the guessing has a time limit, and how long (cancels everybody's ready)
//   missing()                  -> what the players still have to fix before a match could start ("" when nothing)
//   canStart()                 -> nothing is missing and everybody is ready: the match can begin
// Anything not allowed throws invalid_argument with a message that can be shown to the player.
// From 2 to 10 players, and any number of them on each seat: a seat is just a team and a role. With 4 or more players
// the game is BLUE against RED, and each team needs at least one SPY and one GUESSER. With 2 or 3 players the game is
// co-operative (see Match): everybody is on the BLUE team and only the roles matter.
class Lobby {
    private:
        vector<Member> joined;
        set<M_Universe> chosen;          // the universes the match draws its characters from
        TimeLimit clueLimit;             // both off until somebody turns them on
        TimeLimit guessLimit;

        // Players with this seat (team and role).
        int seated(Team team, Role role) const {
            int count = 0;
            for (const Member& member : joined) {
                if (member.seat && *member.seat == Slot(team, role)) count++;
            }
            return count;
        }

        void cancelEveryonesReady() {
            for (Member& member : joined) member.ready = false;
        }

        Member& find(const string& name) {
            for (Member& member : joined) {
                if (member.name == name) return member;
            }
            throw invalid_argument("There is no player named " + name);
        }

        // The name without the spaces around it; throws if it cannot be used by a new player.
        string checkNewName(string name) const {
            size_t first = name.find_first_not_of(" \t\r\n");
            if (first == string::npos) {
                throw invalid_argument("Player name cannot be empty");
            }
            name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
            if (name.size() > maxNameLength) {
                throw invalid_argument("Player name cannot have more than " + to_string(maxNameLength) + " characters");
            }
            for (const Member& member : joined) {
                if (member.name == name) {
                    throw invalid_argument("There is already a player named " + name);
                }
            }
            if (isFull()) {
                throw invalid_argument("The room is full: a game has at most " + to_string(maxPlayers) + " players");
            }
            return name;
        }

    public:
        static constexpr size_t maxNameLength = 30;
        static constexpr int minTurnSeconds = 15;
        static constexpr int maxTurnSeconds = 600;
        static constexpr int minPlayers = 2;
        static constexpr int maxPlayers = 10;
        static constexpr int teamGameMinPlayers = 4;   // with fewer players the game is co-operative

        Lobby() {
            guessLimit.seconds = 90;                 // guessing usually takes longer than a clue
            chosen = {M_Universe::MCU, M_Universe::FOX_XMEN, M_Universe::FANTASTIC_FOUR, M_Universe::COMICS,
                      M_Universe::VENOM, M_Universe::SONY_SPIDERMAN, M_Universe::THROUGH_THE_SPIDERVERSE};   // all of them, until someone picks
        }

        // Fewer than 4 players: everybody plays on the same team, against the cards instead of against another team.
        bool cooperative() const {
            return (int)joined.size() < teamGameMinPlayers;
        }

        // Enters the waiting room. The name is stored without the spaces around it. A new player changes who is going
        // to play, so everybody's ready is cancelled.
        void join(string name) {
            string clean = checkNewName(name);
            cancelEveryonesReady();
            joined.push_back(Member{clean, nullopt, false});
        }

        // Leaves the waiting room, freeing the name and the seat. The others are no longer ready, for the same reason.
        void leave(const string& name) {
            find(name);                      // throws if there is nobody with this name
            for (size_t i = 0; i < joined.size(); i++) {
                if (joined[i].name == name) {
                    joined.erase(joined.begin() + i);
                    break;
                }
            }
            cancelEveryonesReady();
        }

        // Picks a seat, or changes to another one. Not allowed while ready: cancel the ready first.
        void chooseSeat(const string& name, Team team, Role role) {
            Member& member = find(name);
            if (member.ready) {
                throw invalid_argument("Cancel your ready before changing team or role");
            }
            member.seat = Slot(team, role);
        }

        // What is in the way of a match with these players, as a message for them; "" when nothing is.
        // Being ready is not part of it: see canStart().
        string missing() const {
            if ((int)joined.size() < minPlayers) {
                return "At least " + to_string(minPlayers) + " players are needed to start.";
            }
            for (const Member& member : joined) {
                if (!member.seat) return "Everybody needs to pick a team and a role.";
            }
            if (!cooperative()) {
                for (Team team : {Team::BLUE, Team::RED}) {
                    string name = team == Team::BLUE ? "Blue" : "Red";
                    if (seated(team, Role::SPY) == 0) return "The " + name + " team needs a spy.";
                    if (seated(team, Role::GUESSER) == 0) return "The " + name + " team needs a guesser.";
                }
            }
            return "";
        }

        void setReady(const string& name, bool ready) {
            Member& member = find(name);
            if (ready && !member.seat) {
                throw invalid_argument("Pick a team and a role first");
            }
            member.ready = ready;
        }

        // Which universes the characters of the match come from. Changing the choice cancels everybody's ready,
        // so nobody starts a game with characters they did not agree to. (Whether there are enough characters in
        // the chosen universes is up to whoever knows the characters: see Match.)
        void chooseUniverses(const set<M_Universe>& universes) {
            if (universes.empty()) {
                throw invalid_argument("Pick at least one universe");
            }
            if (universes == chosen) return;
            chosen = universes;
            cancelEveryonesReady();
        }

        const set<M_Universe>& universes() const {
            return chosen;
        }

        // Whether the clue (or the guessing) has a time limit, and how many seconds it is. Changing it cancels
        // everybody's ready, so nobody starts a game with rules they did not agree to. The seconds must be valid even
        // when the limit is off.
        void chooseTimer(TimerPhase phase, bool enabled, int seconds) {
            if (seconds < minTurnSeconds || seconds > maxTurnSeconds) {
                throw invalid_argument("The time can be from " + to_string(minTurnSeconds) + " to " + to_string(maxTurnSeconds) + " seconds");
            }
            TimeLimit& limit = phase == TimerPhase::CLUE ? clueLimit : guessLimit;
            if (enabled == limit.enabled && seconds == limit.seconds) return;
            limit.enabled = enabled;
            limit.seconds = seconds;
            cancelEveryonesReady();
        }

        const TimeLimit& timer(TimerPhase phase) const {
            return phase == TimerPhase::CLUE ? clueLimit : guessLimit;
        }

        // Joins, picks the seat and is ready, all at once (for the console, where there is no waiting room).
        void addPlayer(string name, Team team, Role role) {
            name = checkNewName(name);
            joined.push_back(Member{name, Slot(team, role), true});
        }

        // Same as above, for interfaces that send the choices as text (e.g. JSON from a front-end).
        void addPlayer(string name, const string& team, const string& role) {
            addPlayer(name, teamFromString(team), roleFromString(role));
        }

        // Nobody else can enter the waiting room.
        bool isFull() const {
            return (int)joined.size() >= maxPlayers;
        }

        // Nothing is missing (see missing()) and everybody is ready.
        bool canStart() const {
            if (!missing().empty()) return false;
            for (const Member& member : joined) {
                if (!member.ready) return false;
            }
            return true;
        }

        // The match gave everybody their final team and role (it decides them itself in a co-operative game): the
        // seats of the waiting room show them from now on.
        void reseat(const vector<Player>& assigned) {
            for (const Player& player : assigned) {
                find(player.name).seat = Slot(player.team, player.role);
            }
        }

        const vector<Member>& members() const {
            return joined;
        }

        // The players that already have a seat.
        vector<Player> players() const {
            vector<Player> seated;
            for (const Member& member : joined) {
                if (member.seat) seated.push_back(Player(member.name, member.seat->first, member.seat->second));
            }
            return seated;
        }
};
