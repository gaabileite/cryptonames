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
//   join(name)                 -> enter the waiting room, without a seat yet
//   chooseSeat(name, team, role) -> pick or change the seat (not while ready)
//   setReady(name, ready)      -> (un)mark yourself as ready; needs a seat
//   chooseUniverses(set)       -> which universes the characters of the match come from (cancels everybody's ready)
//   chooseTimer(phase, on, seconds) -> whether the clue / the guessing has a time limit, and how long (cancels everybody's ready)
//   canStart()                 -> every seat is taken and everybody is ready: the match can begin
// Anything not allowed throws invalid_argument with a message that can be shown to the player.
// The default rules are 1 SPY and 1 GUESSER per team (4 players); raise the numbers for bigger games.
class Lobby {
    private:
        int spiesPerTeam;
        int guessersPerTeam;
        vector<Member> joined;
        set<M_Universe> chosen;          // the universes the match draws its characters from
        TimeLimit clueLimit;             // both off until somebody turns them on
        TimeLimit guessLimit;

        int seatsFor(Role role) const {
            return role == Role::SPY ? spiesPerTeam : guessersPerTeam;
        }

        // Seats of this kind taken by players other than `except`.
        int takenSeats(Team team, Role role, const string& except = "") const {
            int count = 0;
            for (const Member& member : joined) {
                if (member.name != except && member.seat && *member.seat == Slot(team, role)) count++;
            }
            return count;
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
                throw invalid_argument("The waiting room is full");
            }
            return name;
        }

    public:
        static constexpr size_t maxNameLength = 30;
        static constexpr int minTurnSeconds = 15;
        static constexpr int maxTurnSeconds = 600;

        Lobby(int spiesPerTeam = 1, int guessersPerTeam = 1) {
            this->spiesPerTeam = spiesPerTeam;
            this->guessersPerTeam = guessersPerTeam;
            guessLimit.seconds = 90;                 // guessing usually takes longer than a clue
            chosen = {M_Universe::MCU, M_Universe::FOX_XMEN, M_Universe::FANTASTIC_FOUR, M_Universe::COMICS,
                      M_Universe::VENOM, M_Universe::SONY_SPIDERMAN, M_Universe::THROUGH_THE_SPIDERVERSE};   // all of them, until someone picks
        }

        int totalSeats() const {
            return 2 * (spiesPerTeam + guessersPerTeam);
        }

        // Is there a seat of this kind that nobody has taken?
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

        // Enters the waiting room. The name is stored without the spaces around it.
        void join(string name) {
            joined.push_back(Member{checkNewName(name), nullopt, false});
        }

        // Leaves the waiting room, freeing the name and the seat.
        void leave(const string& name) {
            find(name);                      // throws if there is nobody with this name
            for (size_t i = 0; i < joined.size(); i++) {
                if (joined[i].name == name) {
                    joined.erase(joined.begin() + i);
                    return;
                }
            }
        }

        // Picks a seat, or changes to another one. Not allowed while ready: cancel the ready first.
        void chooseSeat(const string& name, Team team, Role role) {
            Member& member = find(name);
            if (member.ready) {
                throw invalid_argument("Cancel your ready before changing team or role");
            }
            if (takenSeats(team, role, name) >= seatsFor(role)) {
                throw invalid_argument("No " + toString(role) + " seat left on team " + toString(team));
            }
            member.seat = Slot(team, role);
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
            for (Member& member : joined) member.ready = false;
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
            for (Member& member : joined) member.ready = false;
        }

        const TimeLimit& timer(TimerPhase phase) const {
            return phase == TimerPhase::CLUE ? clueLimit : guessLimit;
        }

        // Joins, picks the seat and is ready, all at once (for the console, where there is no waiting room).
        void addPlayer(string name, Team team, Role role) {
            name = checkNewName(name);
            if (!isOpen(team, role)) {
                throw invalid_argument("No " + toString(role) + " seat left on team " + toString(team));
            }
            joined.push_back(Member{name, Slot(team, role), true});
        }

        // Same as above, for interfaces that send the choices as text (e.g. JSON from a front-end).
        void addPlayer(string name, const string& team, const string& role) {
            addPlayer(name, teamFromString(team), roleFromString(role));
        }

        // Nobody else can enter the waiting room.
        bool isFull() const {
            return (int)joined.size() >= totalSeats();
        }

        // Every seat is taken and everybody is ready.
        bool canStart() const {
            if (!isFull()) return false;
            for (const Member& member : joined) {
                if (!member.seat || !member.ready) return false;
            }
            return true;
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
