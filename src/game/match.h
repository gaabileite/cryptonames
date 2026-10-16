#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>
#include "lobby.h"
#include "grid.h"
using namespace std;

// What one player is allowed to see of one card.
struct CardView {
    string characterName;
    string image;       // picture matching the key this player may see, e.g. images/mcu/iron_man_tony_stark/beige.png
    CardKey key;
    CardState state;
};

// Runs one game: owns the players, the board and the turns.
// No input/output here, so any interface can drive it:
//   viewFor(name)         -> the board as that player may see it
//   guess(name, index)    -> the current team's GUESSER reveals a card
//   endTurn(name)         -> the current team's GUESSER stops guessing
//   forfeit(name)         -> a team gives up: any of its players, at any time, and the other team wins
//   advanceClock()        -> with a time limit, passes the turn when the time of a turn has run out
// Anything not allowed throws invalid_argument with a message that can be shown to the player.
//
// Rules: the teams alternate. A GUESSER keeps guessing while they find cards of their own team.
// A card of the other team or a BEIGE card ends the turn; the BLACK card loses the game on the spot.
// The first team to reveal all of its cards wins.
// With a time limit (chosen in the waiting room) each turn can last that many seconds, counted from the moment the turn
// begins; guessing right does not give more time. When it runs out the turn passes to the other team. Nothing runs by
// itself: whoever drives the match calls advanceClock(), and guess() and endTurn() do it first.
class Match {
    public:
        typedef chrono::steady_clock Clock;

    private:
    private:
        vector<Player> participants;
        Grid grid;
        Team turn;
        MatchState matchState;
        Team winningTeam;
        int turnSeconds;                  // 0: no time limit
        Clock::time_point turnStartedAt;
        bool forfeited;
        Team forfeitingTeam;

        static Team opposite(Team team) {
            return team == Team::BLUE ? Team::RED : Team::BLUE;
        }

        const Player& findPlayer(const string& name) const {
            for (const Player& player : participants) {
                if (player.name == name) return player;
            }
            throw invalid_argument("There is no player named " + name);
        }

        // Only the GUESSER of the team whose turn it is can act, and only while the match is going.
        const Player& requireActiveGuesser(const string& name) const {
            if (matchState == MatchState::FINISHED) {
                throw invalid_argument("The match is already over");
            }
            const Player& player = findPlayer(name);
            if (player.role != Role::GUESSER) {
                throw invalid_argument("Only a GUESSER can do this");
            }
            if (player.team != turn) {
                throw invalid_argument("It is not the turn of team " + toString(player.team));
            }
            return player;
        }

        // Only the characters of the universes the lobby chose.
        static vector<Character> allowedCharacters(const Lobby& lobby, const vector<Character>& pool) {
            vector<Character> allowed;
            for (const Character& character : pool) {
                if (lobby.universes().count(character.universe)) allowed.push_back(character);
            }
            return allowed;
        }

        // The turn goes to the other team and its time starts over.
        void passTurn(Clock::time_point now) {
            turn = opposite(turn);
            turnStartedAt = now;
        }

        void finish(Team winner) {
            winningTeam = winner;
            matchState = MatchState::FINISHED;
        }

    public:
        // The lobby must be ready to start (see Lobby::canStart). pool: every character there is; the board draws
        // only from the universes the lobby chose, and throws if those have fewer than 25 characters.
        Match(const Lobby& lobby, const vector<Character>& pool, Clock::time_point now = Clock::now()) : grid(allowedCharacters(lobby, pool)) {
            if (!lobby.canStart()) {
                throw invalid_argument("The match cannot start before every seat is taken and everybody is ready");
            }
            participants = lobby.players();

            grid.assignCharacters();
            grid.assignKeyCards();

            uniform_int_distribution<int> coin(0, 1);
            turn = coin(rng()) == 0 ? Team::BLUE : Team::RED;
            matchState = MatchState::IN_PROGRESS;
            winningTeam = turn;
            turnSeconds = lobby.timer().enabled ? lobby.timer().seconds : 0;
            turnStartedAt = now;
            forfeited = false;
            forfeitingTeam = turn;
        }

        const vector<Player>& players() const {
            return participants;
        }

        Team currentTeam() const {
            return turn;
        }

        bool isFinished() const {
            return matchState == MatchState::FINISHED;
        }

        Team winner() const {
            if (!isFinished()) {
                throw logic_error("The match is not over yet");
            }
            return winningTeam;
        }

        // Did the match end because a team gave up?
        bool wasForfeited() const {
            return forfeited;
        }

        // The team that gave up. Only when wasForfeited().
        Team forfeitedBy() const {
            if (!forfeited) {
                throw logic_error("Nobody forfeited");
            }
            return forfeitingTeam;
        }

        bool hasTimeLimit() const {
            return turnSeconds > 0;
        }

        // How many seconds a turn can last. 0 without a time limit.
        int turnLimitSeconds() const {
            return turnSeconds;
        }

        // Milliseconds left of the current turn (never negative). Only with a time limit and while the match is going on.
        long long millisecondsLeft(Clock::time_point now = Clock::now()) const {
            if (!hasTimeLimit() || isFinished()) {
                throw logic_error("There is no turn timer running");
            }
            long long left = chrono::duration_cast<chrono::milliseconds>(chrono::seconds(turnSeconds) - (now - turnStartedAt)).count();
            return left < 0 ? 0 : left;
        }

        // Passes the turn to the other team once for every whole time limit that went by since the turn began, as if
        // somebody had been watching the clock. Does nothing without a time limit or after the end of the match.
        void advanceClock(Clock::time_point now = Clock::now()) {
            if (!hasTimeLimit() || isFinished()) return;
            Clock::duration limit = chrono::seconds(turnSeconds);
            Clock::duration elapsed = now - turnStartedAt;
            if (elapsed < limit) return;
            long long periods = elapsed / limit;
            if (periods % 2 == 1) turn = opposite(turn);
            turnStartedAt += limit * periods;
        }

        // How many cards of the team are still face down.
        int remaining(Team team) const {
            int count = 0;
            for (const Card& card : grid.lineup) {
                if (card.key == keyOf(team) && card.state == CardState::SECRET) count++;
            }
            return count;
        }

        // The 25 cards as that player may see them: a SPY sees every key,
        // a GUESSER only sees the key of the cards that were already revealed (the rest look BEIGE).
        vector<CardView> viewFor(const string& name) const {
            const Player& player = findPlayer(name);
            vector<CardView> view;
            for (const Card& card : grid.lineup) {
                bool visible = player.isSpy() || card.state == CardState::REVEALED;
                CardKey shown = visible ? card.key : CardKey::BEIGE;
                view.push_back({card.characterName, card.imageFor(shown), shown, card.state});
            }
            return view;
        }

        // Reveals the card at index (0-24) and returns its key.
        CardKey guess(const string& name, int index, Clock::time_point now = Clock::now()) {
            advanceClock(now);                    // a guess that comes after the time is up is for the next team's turn
            const Player& player = requireActiveGuesser(name);
            if (index < 0 || index >= 25) {
                throw invalid_argument("The card must be between 0 and 24");
            }
            if (grid.lineup[index].state == CardState::REVEALED) {
                throw invalid_argument("That card was already revealed");
            }

            CardKey key = grid.lineup[index].reveal();
            Team other = opposite(player.team);

            if (key == CardKey::BLACK) {
                finish(other);
            } else if (remaining(player.team) == 0) {
                finish(player.team);
            } else if (remaining(other) == 0) {
                finish(other);
            } else if (key != keyOf(player.team)) {
                passTurn(now);
            }
            return key;
        }

        void endTurn(const string& name, Clock::time_point now = Clock::now()) {
            advanceClock(now);
            requireActiveGuesser(name);
            passTurn(now);
        }

        // The player's team gives up and the other team wins. Any player of the team can do it (spy or guesser),
        // at any time of the match, even when it is not their team's turn.
        void forfeit(const string& name) {
            if (matchState == MatchState::FINISHED) {
                throw invalid_argument("The match is already over");
            }
            const Player& player = findPlayer(name);
            forfeited = true;
            forfeitingTeam = player.team;
            finish(opposite(player.team));
        }
};
