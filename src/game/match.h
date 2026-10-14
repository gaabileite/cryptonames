#pragma once
#include <string>
#include <vector>
#include <stdexcept>
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
// Anything not allowed throws invalid_argument with a message that can be shown to the player.
//
// Rules: the teams alternate. A GUESSER keeps guessing while they find cards of their own team.
// A card of the other team or a BEIGE card ends the turn; the BLACK card loses the game on the spot.
// The first team to reveal all of its cards wins.
class Match {
    private:
        vector<Player> participants;
        Grid grid;
        Team turn;
        MatchState matchState;
        Team winningTeam;
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

        void finish(Team winner) {
            winningTeam = winner;
            matchState = MatchState::FINISHED;
        }

    public:
        // The lobby must be ready to start (see Lobby::canStart). pool: every character the board may draw from.
        Match(const Lobby& lobby, vector<Character> pool) : grid(pool) {
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
        CardKey guess(const string& name, int index) {
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
                turn = other;
            }
            return key;
        }

        void endTurn(const string& name) {
            const Player& player = requireActiveGuesser(name);
            turn = opposite(player.team);
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
