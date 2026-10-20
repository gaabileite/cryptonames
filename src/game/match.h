#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>
#include <set>
#include "lobby.h"
#include "grid.h"
#include "clue.h"
using namespace std;

// What one player is allowed to see of one card.
struct CardView {
    string characterName;
    string image;       // picture matching the key this player may see, e.g. images/mcu/iron_man_tony_stark/beige.png
    CardKey key;
    CardState state;
    vector<Team> pins;  // the teams that pinned this card (a reminder, visible to everybody); BLUE first
};

// Runs one game: owns the players, the board and the turns.
// No input/output here, so any interface can drive it:
//   viewFor(name)         -> the board as that player may see it
//   giveClue(name, text, number) -> the current team's SPY gives the clue of the turn: 1-2 words and how many cards fit
//   guess(name, index)    -> the current team's GUESSER reveals a card (after the clue; one try each)
//   endTurn(name)         -> the current team's GUESSER stops guessing
//   forfeit(name)         -> a team gives up: any of its players, at any time, and the other team wins
//   playerLeft(name)      -> a player left the room: their team goes on, unless it is left without a spy or a guesser
//   addPlayer(name, team, role) -> a player who came into the room after the start takes a seat (co-operative: always BLUE)
//   setPin(name, index, pinned) -> a player pins a card (or takes the pin off) for their team: a reminder that every player
//                               sees, in the colour of the team. Both teams can pin the same card. A card that is picked
//                               loses its pins.
//   advanceClock()        -> with a time limit, passes the turn when the time of a turn has run out
// Anything not allowed throws invalid_argument with a message that can be shown to the player.
//
// Rules: the teams alternate. A turn starts with the SPY's clue, and the GUESSER can only guess after it.
// A team can have several SPYs and several GUESSERs: any SPY of the team on turn gives the clue, any GUESSER guesses.
// The clue number is how many tries the GUESSER has. A try that finds a card of their own team is a good one and the
// GUESSER keeps going until the tries run out. A card of the other team or a BEIGE card ends the turn at once, and
// the tries that were left for the clue (the clue number minus the good tries) are added to the next clue of that
// team. Ending the turn on purpose, or running out of time, gives no extra tries.
// The BLACK card loses the game on the spot.
// The first team to reveal all of its cards wins.
// With fewer than 4 players the game is co-operative: everybody is on the BLUE team (the lobby's choice of team does
// not matter, and the roles they picked are kept, unless everybody picked the same one: then one player, at random,
// takes the other role). There is no RED team, so after every turn it is BLUE's turn again; a RED card is a wrong
// card like a BEIGE one, and the team wins by finding all the BLUE cards. The BLACK card still loses.
// There can be two time limits (chosen in the waiting room): one for the SPY to give the clue, counted from the moment
// the turn begins, and one for the GUESSER, counted from the moment the clue is given; guessing right does not give more
// time. When the time of a phase runs out the turn passes to the other team. Nothing runs by itself: whoever drives the
// match calls advanceClock(), and giveClue(), guess() and endTurn() do it first.
class Match {
    public:
        typedef chrono::steady_clock Clock;

    private:
        vector<Player> participants;
        Grid grid;
        Team turn;
        MatchState matchState;
        Team winningTeam;
        int clueSeconds;                  // 0: no time limit for the clue
        int guessSeconds;                 // 0: no time limit for the guessing
        Clock::time_point phaseStartedAt; // when the turn began, or when the clue was given
        bool forfeited;
        Team forfeitingTeam;
        string forfeitingPlayer;          // who gave up (or left, and left their team without a spy or a guesser)
        bool coop;                        // co-operative game: everybody is on the BLUE team
        set<string> departed;             // players who left the room during the match
        vector<Clue> clues;               // every clue of the match, oldest first
        bool clueGiven;                   // has the SPY of the team on turn given the clue of this turn?
        int triesLeft;                    // tries left for the clue of this turn
        int goodTries;                    // good tries (cards of the team found) in this turn
        int carryOver[2];                 // extra tries waiting for the next clue of each team (indexed by sideOf)
        set<int> pinned[2];               // the cards each team pinned (indexed by sideOf)

        static int sideOf(Team team) {
            return team == Team::BLUE ? 0 : 1;
        }

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

        // Everybody on BLUE, with the roles they picked. If those are all the same, one player picked at random takes the other.
        static vector<Player> cooperativeLineup(vector<Player> players) {
            bool anySpy = false, anyGuesser = false;
            for (Player& player : players) {
                player.team = Team::BLUE;
                (player.isSpy() ? anySpy : anyGuesser) = true;
            }
            if (!anySpy || !anyGuesser) {
                uniform_int_distribution<size_t> pick(0, players.size() - 1);
                Player& switched = players[pick(rng())];
                switched.role = anySpy ? Role::GUESSER : Role::SPY;
            }
            return players;
        }

        // Does the team of this player still have a spy and a guesser if they are not there?
        bool teamKeepsPlaying(const Player& leaving) const {
            bool spy = false, guesser = false;
            for (const Player& player : participants) {
                if (player.name == leaving.name || departed.count(player.name) || player.team != leaving.team) continue;
                (player.isSpy() ? spy : guesser) = true;
            }
            return spy && guesser;
        }

        // Only the characters of the universes the lobby chose.
        static vector<Character> allowedCharacters(const Lobby& lobby, const vector<Character>& pool) {
            vector<Character> allowed;
            for (const Character& character : pool) {
                if (lobby.universes().count(character.universe)) allowed.push_back(character);
            }
            return allowed;
        }

        // A new turn begins: nobody has given the clue yet.
        void startTurn(Clock::time_point now) {
            phaseStartedAt = now;
            clueGiven = false;
            triesLeft = 0;
            goodTries = 0;
        }

        // The turn goes to the other team and its time starts over. In a co-operative game there is no other team.
        void passTurn(Clock::time_point now) {
            if (!coop) turn = opposite(turn);
            startTurn(now);
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
                throw invalid_argument("The match cannot start before the players are seated and everybody is ready");
            }
            coop = lobby.cooperative();
            participants = coop ? cooperativeLineup(lobby.players()) : lobby.players();

            grid.assignCharacters();
            grid.assignKeyCards();

            uniform_int_distribution<int> coin(0, 1);
            turn = coop || coin(rng()) == 0 ? Team::BLUE : Team::RED;
            matchState = MatchState::IN_PROGRESS;
            winningTeam = turn;
            clueSeconds = lobby.timer(TimerPhase::CLUE).enabled ? lobby.timer(TimerPhase::CLUE).seconds : 0;
            guessSeconds = lobby.timer(TimerPhase::GUESS).enabled ? lobby.timer(TimerPhase::GUESS).seconds : 0;
            carryOver[0] = carryOver[1] = 0;
            startTurn(now);
            forfeited = false;
            forfeitingTeam = turn;
        }

        // Everybody who started the match, with the team and role they play (also those who left since).
        const vector<Player>& players() const {
            return participants;
        }

        // A co-operative game: everybody is on the BLUE team, and winner() is RED when they lose.
        bool isCooperative() const {
            return coop;
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

        // The player who gave up. Only when wasForfeited().
        const string& forfeitedByPlayer() const {
            if (!forfeited) {
                throw logic_error("Nobody forfeited");
            }
            return forfeitingPlayer;
        }

        // The team that gave up. Only when wasForfeited().
        Team forfeitedBy() const {
            if (!forfeited) {
                throw logic_error("Nobody forfeited");
            }
            return forfeitingTeam;
        }

        // Seconds of the limit of the phase the turn is in: giving the clue, or guessing after it. 0 means no limit.
        int phaseLimitSeconds() const {
            return clueGiven ? guessSeconds : clueSeconds;
        }

        // Does the phase the turn is in have a time limit?
        bool hasTimeLimit() const {
            return phaseLimitSeconds() > 0;
        }

        // The limits chosen for the match, whatever the phase is. 0 means no limit.
        int clueLimitSeconds() const {
            return clueSeconds;
        }

        int guessLimitSeconds() const {
            return guessSeconds;
        }

        // Milliseconds left of the current phase (never negative). Only while the match is going on and the phase has a limit.
        long long millisecondsLeft(Clock::time_point now = Clock::now()) const {
            if (!hasTimeLimit() || isFinished()) {
                throw logic_error("There is no timer running");
            }
            long long left = chrono::duration_cast<chrono::milliseconds>(chrono::seconds(phaseLimitSeconds()) - (now - phaseStartedAt)).count();
            return left < 0 ? 0 : left;
        }

        // Passes the turn to the other team each time the time of the phase has run out, as if somebody had been watching
        // the clock. Does nothing when the phase has no limit or the match is over.
        void advanceClock(Clock::time_point now = Clock::now()) {
            while (!isFinished() && hasTimeLimit()) {
                Clock::time_point deadline = phaseStartedAt + chrono::seconds(phaseLimitSeconds());
                if (now < deadline) return;
                passTurn(deadline);                   // the other team's turn begins at the moment time ran out
            }
        }

        // Has the SPY given the clue of this turn?
        bool hasClue() const {
            return clueGiven;
        }

        // The clue of this turn. Only when hasClue().
        const Clue& currentClue() const {
            if (!clueGiven) {
                throw logic_error("There is no clue yet");
            }
            return clues.back();
        }

        // Tries the GUESSER still has in this turn (0 before the clue).
        int triesRemaining() const {
            return triesLeft;
        }

        // Extra tries that the next clue of this team will bring, because of the cards it missed.
        int pendingExtraTries(Team team) const {
            return carryOver[sideOf(team)];
        }

        // Every clue given so far, oldest first.
        const vector<Clue>& clueHistory() const {
            return clues;
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
        // Once the match is over (a win, the black card, a forfeit) every player sees every key; `state` still says which
        // cards were actually picked.
        vector<CardView> viewFor(const string& name) const {
            const Player& player = findPlayer(name);
            vector<CardView> view;
            for (int i = 0; i < 25; i++) {
                const Card& card = grid.lineup[i];
                bool visible = player.isSpy() || card.state == CardState::REVEALED || isFinished();   // when the match is over everybody sees every card
                CardKey shown = visible ? card.key : CardKey::BEIGE;
                vector<Team> pins;
                for (Team team : {Team::BLUE, Team::RED}) {
                    if (pinned[sideOf(team)].count(i)) pins.push_back(team);
                }
                view.push_back({card.characterName, card.imageFor(shown), shown, card.state, pins});
            }
            return view;
        }

        // The SPY of the team on turn gives the clue: 1 or 2 words and how many cards fit it (from 1 to the cards the
        // team still has to find). The GUESSER gets that many tries, plus the extra ones left from the previous turn.
        void giveClue(const string& name, const string& text, int number, Clock::time_point now = Clock::now()) {
            advanceClock(now);
            if (matchState == MatchState::FINISHED) {
                throw invalid_argument("The match is already over");
            }
            const Player& player = findPlayer(name);
            if (player.role != Role::SPY) {
                throw invalid_argument("Only the SPY gives the clues");
            }
            if (player.team != turn) {
                throw invalid_argument("It is not the turn of team " + toString(player.team));
            }
            if (clueGiven) {
                throw invalid_argument("The clue of this turn was already given");
            }
            string clean = cleanClueText(text);
            int cardsLeft = remaining(player.team);
            if (number < 1 || number > cardsLeft) {
                throw invalid_argument("The number must be from 1 to " + to_string(cardsLeft) + ", the cards your team still has to find");
            }

            int extra = carryOver[sideOf(player.team)];
            carryOver[sideOf(player.team)] = 0;
            clues.push_back(Clue{player.team, clean, number, extra, 0});
            clueGiven = true;
            phaseStartedAt = now;                 // the time to guess starts now
            triesLeft = number + extra;
            goodTries = 0;
        }

        // Pins the card at index (0-24) for the player's team, or takes the team's pin off. Any player of the team can do it,
        // spy or guesser, at any moment of the match; it changes nothing in the game. Whether the card is pinned afterwards
        // does not depend on what it was before, so two players asking for the same thing do not undo each other.
        void setPin(const string& name, int index, bool pin) {
            if (matchState == MatchState::FINISHED) {
                throw invalid_argument("The match is already over");
            }
            const Player& player = findPlayer(name);
            if (index < 0 || index >= 25) {
                throw invalid_argument("The card must be between 0 and 24");
            }
            if (grid.lineup[index].state == CardState::REVEALED) {
                throw invalid_argument("That card was already revealed");
            }
            if (pin) pinned[sideOf(player.team)].insert(index);
            else pinned[sideOf(player.team)].erase(index);
        }

        // Reveals the card at index (0-24) and returns its key. Costs one try.
        CardKey guess(const string& name, int index, Clock::time_point now = Clock::now()) {
            advanceClock(now);                    // a guess that comes after the time is up is for the next team's turn
            const Player& player = requireActiveGuesser(name);
            if (!clueGiven) {
                throw invalid_argument("Wait for your spy's clue");
            }
            if (index < 0 || index >= 25) {
                throw invalid_argument("The card must be between 0 and 24");
            }
            if (grid.lineup[index].state == CardState::REVEALED) {
                throw invalid_argument("That card was already revealed");
            }

            CardKey key = grid.lineup[index].reveal();
            pinned[0].erase(index);               // a picked card is not a reminder any more
            pinned[1].erase(index);
            Team other = opposite(player.team);

            if (key == CardKey::BLACK) {
                finish(other);
            } else if (remaining(player.team) == 0) {
                finish(player.team);
            } else if (!coop && remaining(other) == 0) {
                finish(other);
            } else if (key != keyOf(player.team)) {
                // A wrong card: the turn is over, and the tries that were not used well go to the next clue.
                carryOver[sideOf(player.team)] = max(0, clues.back().number - goodTries);
                passTurn(now);
            } else {
                goodTries++;
                clues.back().found++;
                triesLeft--;
                if (triesLeft == 0) {
                    passTurn(now);                // all the tries were good ones: nothing is carried over
                }
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
            forfeitingPlayer = player.name;
            finish(opposite(player.team));
        }

        // How many players are in the match right now (not counting those who left).
        int playing() const {
            return (int)participants.size() - (int)departed.size();
        }

        // Would the team of this player have to give up if they left now?
        bool leavingForfeits(const string& name) const {
            return !isFinished() && !teamKeepsPlaying(findPlayer(name));
        }

        // A player who came into the room after the match started takes a seat, and keeps it until the end. In a
        // co-operative game the team is always BLUE, whatever they ask for. Returns the seat they got. A player who left
        // earlier can come back with the same name: their old seat is forgotten.
        Player addPlayer(const string& name, Team team, Role role) {
            if (isFinished()) {
                throw invalid_argument("The match is already over");
            }
            for (size_t i = 0; i < participants.size(); i++) {
                if (participants[i].name != name) continue;
                if (!departed.count(name)) {
                    throw invalid_argument("There is already a player named " + name);
                }
                departed.erase(name);
                participants.erase(participants.begin() + i);
                break;
            }
            participants.push_back(Player(name, coop ? Team::BLUE : team, role));
            return participants.back();
        }

        // The player left the room. Their team goes on without them, unless it has no spy or no guesser left: then it
        // forfeits, so nobody waits for a team that cannot play. Does nothing once the match is over.
        void playerLeft(const string& name) {
            if (isFinished()) return;
            if (leavingForfeits(name)) {
                forfeit(name);
            }
            departed.insert(name);
        }
};
