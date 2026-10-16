// Tests for the game rules (Lobby, Match, Grid, enums), no database or network needed.
// Build and run: make test
#include <map>
#include <set>
#include "game/lobby.h"
#include "game/match.h"

int failures = 0;

#define CHECK(condition) \
    do { if (!(condition)) { cout << "FAIL line " << __LINE__ << ": " #condition << endl; failures++; } } while (0)

template <class F> bool rejects(F action) {
    try { action(); } catch (const invalid_argument&) { return true; }
    return false;
}

Lobby fullLobby() {
    Lobby lobby;
    lobby.addPlayer("bs", "BLUE", "SPY");
    lobby.addPlayer("bg", "BLUE", "GUESSER");
    lobby.addPlayer("rs", "RED", "SPY");
    lobby.addPlayer("rg", "RED", "GUESSER");
    return lobby;
}

vector<Character> characters(int count = 30) {
    vector<Character> pool;
    for (int i = 0; i < count; i++) {
        pool.push_back(Character("C" + to_string(i), M_CharacterType::PERSON, M_Universe::MCU, "images/c" + to_string(i)));
    }
    return pool;
}

string guesserOf(Team team) { return team == Team::BLUE ? "bg" : "rg"; }
string spyOf(Team team) { return team == Team::BLUE ? "bs" : "rs"; }
Team otherThan(Team team) { return team == Team::BLUE ? Team::RED : Team::BLUE; }

// The SPY of the team on turn gives a clue; by default it fits every card the team still has to find.
void clueFor(Match& match, int number = -1, Match::Clock::time_point now = Match::Clock::now()) {
    Team team = match.currentTeam();
    if (number < 0) number = match.remaining(team);
    match.giveClue(spyOf(team), "TEST CLUE", number, now);
}

void testEnums() {
    for (string s : {"MCU", "FOX X-MEN", "FANTASTIC FOUR", "COMICS", "VENOM", "SONY SPIDER-MAN", "THROUGH THE SPIDERVERSE"}) {
        CHECK(toString(universeFromString(s)) == s);
    }
    for (string s : {"Person", "Object"}) CHECK(toString(characterTypeFromString(s)) == s);
    for (string s : {"BLUE", "RED", "BEIGE", "BLACK"}) CHECK(toString(cardKeyFromString(s)) == s);
    CHECK(rejects([] { universeFromString("DC"); }));
    CHECK(rejects([] { teamFromString("GREEN"); }));
    CHECK(rejects([] { roleFromString("BOSS"); }));
}

void testLobby() {
    Lobby lobby;
    CHECK(lobby.totalSeats() == 4 && lobby.openSlots().size() == 4 && !lobby.isFull());

    lobby.addPlayer("Ana", Team::BLUE, Role::SPY);
    CHECK(!lobby.isOpen(Team::BLUE, Role::SPY) && lobby.openSlots().size() == 3);
    CHECK(rejects([&] { lobby.addPlayer("Bia", Team::BLUE, Role::SPY); }));      // seat taken
    CHECK(rejects([&] { lobby.addPlayer("Ana", Team::RED, Role::SPY); }));       // duplicate name
    CHECK(rejects([&] { lobby.addPlayer("  Ana ", Team::RED, Role::SPY); }));    // duplicate once trimmed
    CHECK(rejects([&] { lobby.addPlayer("", Team::RED, Role::SPY); }));
    CHECK(rejects([&] { lobby.addPlayer(" \t ", Team::RED, Role::SPY); }));      // only blanks
    CHECK(rejects([&] { lobby.addPlayer(string(Lobby::maxNameLength + 1, 'x'), Team::RED, Role::SPY); }));
    CHECK(rejects([&] { lobby.addPlayer("Bia", "GREEN", "SPY"); }));
    CHECK(rejects([&] { lobby.addPlayer("Bia", "RED", "BOSS"); }));
    CHECK(lobby.players().size() == 1);                                          // rejections add nothing

    lobby.addPlayer("  Bia  ", "BLUE", "GUESSER");
    CHECK(lobby.players()[1].name == "Bia");                                     // stored trimmed
    lobby.addPlayer(string(Lobby::maxNameLength, 'x'), "RED", "SPY");
    CHECK(!lobby.isFull());
    lobby.addPlayer("Dani", Team::RED, Role::GUESSER);
    CHECK(lobby.isFull() && lobby.openSlots().empty());

    Lobby big(2, 3);
    CHECK(big.totalSeats() == 10 && big.openSlots().size() == 10);
}

void testWaitingRoom() {
    Lobby lobby;
    lobby.join("  Ana ");
    CHECK(lobby.members().size() == 1 && lobby.members()[0].name == "Ana");     // trimmed, no seat yet
    CHECK(!lobby.members()[0].seat && !lobby.members()[0].ready);
    CHECK(lobby.players().empty() && lobby.openSlots().size() == 4);            // joining takes no seat
    CHECK(rejects([&] { lobby.join("Ana"); }));                                // duplicate name
    CHECK(rejects([&] { lobby.join("   "); }));
    CHECK(rejects([&] { lobby.setReady("Ana", true); }));                      // needs a seat first
    CHECK(rejects([&] { lobby.chooseSeat("Nobody", Team::BLUE, Role::SPY); }));
    CHECK(rejects([&] { lobby.setReady("Nobody", true); }));

    lobby.chooseSeat("Ana", Team::BLUE, Role::SPY);
    CHECK(lobby.openSlots().size() == 3 && !lobby.isOpen(Team::BLUE, Role::SPY));
    lobby.chooseSeat("Ana", Team::BLUE, Role::SPY);                            // the seat she already has is fine
    lobby.chooseSeat("Ana", Team::RED, Role::GUESSER);                         // changing frees the old seat
    CHECK(lobby.isOpen(Team::BLUE, Role::SPY) && !lobby.isOpen(Team::RED, Role::GUESSER));

    lobby.join("Bia");
    CHECK(rejects([&] { lobby.chooseSeat("Bia", Team::RED, Role::GUESSER); })); // taken by Ana
    lobby.chooseSeat("Bia", Team::BLUE, Role::SPY);                            // the seat Ana left

    lobby.setReady("Ana", true);
    CHECK(rejects([&] { lobby.chooseSeat("Ana", Team::BLUE, Role::GUESSER); })); // locked while ready
    lobby.setReady("Ana", false);
    lobby.chooseSeat("Ana", Team::BLUE, Role::GUESSER);

    lobby.join("Cris");
    lobby.join("Dani");
    CHECK(lobby.isFull() && rejects([&] { lobby.join("Eve"); }));              // only as many players as seats
    CHECK(!lobby.canStart());
    lobby.chooseSeat("Cris", Team::RED, Role::SPY);
    lobby.chooseSeat("Dani", Team::RED, Role::GUESSER);
    CHECK(lobby.players().size() == 4 && lobby.openSlots().empty());
    for (string name : {"Ana", "Bia", "Cris"}) lobby.setReady(name, true);
    CHECK(!lobby.canStart());                                                  // Dani is not ready yet
    CHECK(rejects([&] { Match match(lobby, characters()); }));
    lobby.setReady("Dani", true);
    CHECK(lobby.canStart());
    lobby.setReady("Cris", false);                                             // anyone can cancel until the match starts
    CHECK(!lobby.canStart());
    lobby.setReady("Cris", true);

    Match match(lobby, characters());                                          // starts with the seats they picked
    CHECK(match.players().size() == 4);
    CHECK(match.viewFor("Bia").size() == 25);

    // Leaving frees the name and the seat; with unseated players it still cannot start
    Lobby other;
    other.join("A"); other.chooseSeat("A", Team::BLUE, Role::SPY); other.setReady("A", true);
    other.leave("A");
    CHECK(other.members().empty() && other.isOpen(Team::BLUE, Role::SPY));
    CHECK(rejects([&] { other.leave("A"); }));
    other.join("A");                                                           // the name is free again
    Lobby unseated;
    for (string name : {"a", "b", "c", "d"}) unseated.join(name);
    CHECK(unseated.isFull() && !unseated.canStart());

    // addPlayer (console): joins, sits and is ready in one step
    Lobby console = fullLobby();
    CHECK(console.canStart());
}

void testUniverses() {
    // 30 characters from MCU, 30 from FOX X-MEN, 10 from FANTASTIC FOUR
    vector<Character> mixed;
    for (int i = 0; i < 30; i++) mixed.push_back(Character("M" + to_string(i), M_CharacterType::PERSON, M_Universe::MCU, "images/m" + to_string(i)));
    for (int i = 0; i < 30; i++) mixed.push_back(Character("X" + to_string(i), M_CharacterType::PERSON, M_Universe::FOX_XMEN, "images/x" + to_string(i)));
    for (int i = 0; i < 10; i++) mixed.push_back(Character("F" + to_string(i), M_CharacterType::PERSON, M_Universe::FANTASTIC_FOUR, "images/f" + to_string(i)));

    Lobby lobby = fullLobby();
    CHECK(lobby.universes().size() == 7 && lobby.universes().count(M_Universe::MCU));     // everything, until somebody picks
    CHECK(lobby.canStart());

    CHECK(rejects([&] { lobby.chooseUniverses({}); }));                                  // at least one
    CHECK(lobby.canStart());                                                             // a rejected choice changes nothing

    lobby.chooseUniverses(lobby.universes());                                            // the same choice again: nobody is un-readied
    CHECK(lobby.canStart());

    lobby.chooseUniverses({M_Universe::FOX_XMEN});
    CHECK(lobby.universes().size() == 1 && lobby.universes().count(M_Universe::FOX_XMEN));
    CHECK(!lobby.canStart());                                                            // a new choice cancels everybody's ready
    for (const Member& member : lobby.members()) CHECK(!member.ready);
    CHECK(rejects([&] { Match match(lobby, mixed); }));
    for (string name : {"bs", "bg", "rs", "rg"}) lobby.setReady(name, true);

    { // the board only has characters of the chosen universe
        Match match(lobby, mixed);
        for (const CardView& card : match.viewFor("bg")) CHECK(card.characterName[0] == 'X');
    }
    for (int round = 0; round < 100; round++) {                                          // two universes: both can show up, never the third
        lobby.chooseUniverses({M_Universe::MCU, M_Universe::FOX_XMEN});
        for (string name : {"bs", "bg", "rs", "rg"}) lobby.setReady(name, true);
        Match match(lobby, mixed);
        for (const CardView& card : match.viewFor("bs")) CHECK(card.characterName[0] == 'M' || card.characterName[0] == 'X');
    }

    // too few characters in the chosen universes: the match refuses to start
    lobby.chooseUniverses({M_Universe::FANTASTIC_FOUR});
    for (string name : {"bs", "bg", "rs", "rg"}) lobby.setReady(name, true);
    CHECK(rejects([&] { Match match(lobby, mixed); }));
    lobby.chooseUniverses({M_Universe::FANTASTIC_FOUR, M_Universe::MCU});
    for (string name : {"bs", "bg", "rs", "rg"}) lobby.setReady(name, true);
    Match match(lobby, mixed);
    CHECK(match.viewFor("bs").size() == 25);
}

void testTimer() {
    typedef Match::Clock Clock;
    auto at = [](Clock::time_point start, int seconds) { return start + chrono::seconds(seconds); };

    // ---- the waiting room setting
    Lobby lobby = fullLobby();
    CHECK(!lobby.timer().enabled && lobby.timer().seconds == 60);                  // off, 60 s once turned on
    CHECK(rejects([&] { lobby.chooseTimer(true, Lobby::minTurnSeconds - 1); }));
    CHECK(rejects([&] { lobby.chooseTimer(true, Lobby::maxTurnSeconds + 1); }));
    CHECK(rejects([&] { lobby.chooseTimer(false, 5); }));                          // the seconds must be valid even when off
    CHECK(rejects([&] { lobby.chooseTimer(true, -3); }));
    CHECK(lobby.canStart() && !lobby.timer().enabled);                             // rejected choices change nothing
    lobby.chooseTimer(false, 60);                                                  // the same choice: nobody is un-readied
    CHECK(lobby.canStart());
    lobby.chooseTimer(true, 90);
    CHECK(lobby.timer().enabled && lobby.timer().seconds == 90 && !lobby.canStart());   // a new choice cancels everybody's ready
    for (string name : {"bs", "bg", "rs", "rg"}) lobby.setReady(name, true);
    lobby.chooseTimer(true, Lobby::minTurnSeconds);                                // the limits are allowed
    lobby.chooseTimer(true, Lobby::maxTurnSeconds);
    lobby.chooseTimer(false, 120);                                                 // off, but remembers the seconds
    CHECK(!lobby.timer().enabled && lobby.timer().seconds == 120);

    // ---- no timer: time never matters
    {
        Clock::time_point start = Clock::now();
        Match match(fullLobby(), characters(), start);
        CHECK(!match.hasTimeLimit() && match.turnLimitSeconds() == 0);
        Team team = match.currentTeam();
        match.advanceClock(at(start, 100000));
        CHECK(match.currentTeam() == team);
        try { match.millisecondsLeft(start); CHECK(false); } catch (const logic_error&) {}
    }

    // ---- with a timer of 30 s
    Lobby timed = fullLobby();
    timed.chooseTimer(true, 30);
    for (string name : {"bs", "bg", "rs", "rg"}) timed.setReady(name, true);
    {
        Clock::time_point start = Clock::now();
        Match match(timed, characters(), start);
        Team team = match.currentTeam(), other = otherThan(team);
        CHECK(match.hasTimeLimit() && match.turnLimitSeconds() == 30);
        CHECK(match.millisecondsLeft(start) == 30000);
        CHECK(match.millisecondsLeft(at(start, 12)) == 18000);
        CHECK(match.millisecondsLeft(at(start, 99)) == 0);                         // never negative

        match.advanceClock(at(start, 29));
        CHECK(match.currentTeam() == team);                                        // not yet
        match.advanceClock(at(start, 30));
        CHECK(match.currentTeam() == other);                                       // time is up: the turn passes
        CHECK(match.millisecondsLeft(at(start, 30)) == 30000);                     // and the new turn has all its time
        CHECK(match.millisecondsLeft(at(start, 45)) == 15000);
        match.advanceClock(at(start, 59));
        CHECK(match.currentTeam() == other);
        match.advanceClock(at(start, 60));
        CHECK(match.currentTeam() == team);
        match.advanceClock(at(start, 60));                                         // calling it again changes nothing
        CHECK(match.currentTeam() == team);
        CHECK(!match.isFinished() && match.remaining(Team::BLUE) == 8 && match.remaining(Team::RED) == 8);   // no card is touched
    }
    { // nobody looked at the clock for a long time: it is as if somebody had been watching it
        Clock::time_point start = Clock::now();
        Match match(timed, characters(), start);
        Team team = match.currentTeam();
        match.advanceClock(at(start, 30 * 5 + 7));                                 // five full turns went by: an odd number
        CHECK(match.currentTeam() == otherThan(team));
        CHECK(match.millisecondsLeft(at(start, 30 * 5 + 7)) == 23000);             // and 7 s of the sixth turn are gone
        match.advanceClock(at(start, 30 * 6 + 1));
        CHECK(match.currentTeam() == team);                                        // six full turns: back to the first team
    }
    { // a guess after the time is up is a guess in the other team's turn
        Clock::time_point start = Clock::now();
        Match match(timed, characters(), start);
        Team team = match.currentTeam();
        CHECK(rejects([&] { match.guess(guesserOf(team), 0, at(start, 31)); }));   // too late for them
        CHECK(match.currentTeam() == otherThan(team));
        CHECK(match.remaining(Team::BLUE) == 8 && match.remaining(Team::RED) == 8);
        clueFor(match, 3, at(start, 31));                                          // the other team's turn: their spy gives the clue
        match.guess(guesserOf(otherThan(team)), 0, at(start, 31));                 // and their guesser can play
        CHECK(match.viewFor("bs")[0].state == CardState::REVEALED);               // and their guess counted
    }
    { // ending the turn, or a wrong guess, gives the next team a full turn; a right guess gives no extra time
        Clock::time_point start = Clock::now();
        Match match(timed, characters(), start);
        Team team = match.currentTeam(), other = otherThan(team);
        vector<CardView> spy = match.viewFor(spyOf(team));
        int mine = -1, mine2 = -1, beige = -1;
        for (int i = 0; i < 25; i++) {
            if (spy[i].key == keyOf(team)) (mine < 0 ? mine : mine2) = i;
            if (spy[i].key == CardKey::BEIGE) beige = i;
        }
        clueFor(match, 5, at(start, 3));                                           // the clue takes part of the 30 s too
        match.guess(guesserOf(team), mine, at(start, 20));                         // right: the turn goes on...
        CHECK(match.currentTeam() == team && match.millisecondsLeft(at(start, 20)) == 10000);   // ...and the clock was not reset
        match.guess(guesserOf(team), mine2, at(start, 25));
        CHECK(match.millisecondsLeft(at(start, 25)) == 5000);
        match.guess(guesserOf(team), beige, at(start, 28));                        // wrong: the turn passes
        CHECK(match.currentTeam() == other && match.millisecondsLeft(at(start, 28)) == 30000);
        clueFor(match, 2, at(start, 29));
        match.endTurn(guesserOf(other), at(start, 40));                            // ending it also starts the next one fresh
        CHECK(match.currentTeam() == team && match.millisecondsLeft(at(start, 40)) == 30000);
        match.advanceClock(at(start, 69));
        CHECK(match.currentTeam() == team);
        match.advanceClock(at(start, 70));
        CHECK(match.currentTeam() == other);
    }
    { // after the end of the match the clock stops
        Clock::time_point start = Clock::now();
        Match match(timed, characters(), start);
        Team team = match.currentTeam();
        match.forfeit(guesserOf(team));
        Team winner = match.winner();
        match.advanceClock(at(start, 1000));
        CHECK(match.winner() == winner && match.isFinished());
        try { match.millisecondsLeft(at(start, 1)); CHECK(false); } catch (const logic_error&) {}
        CHECK(rejects([&] { match.endTurn(guesserOf(match.currentTeam()), at(start, 2)); }));
    }
}

// The indexes of the cards with that key, as the spy sees them.
vector<int> cardsWith(const Match& match, const string& spy, CardKey key) {
    vector<int> found;
    vector<CardView> view = match.viewFor(spy);
    for (int i = 0; i < 25; i++) if (view[i].key == key) found.push_back(i);
    return found;
}

void testClues() {
    typedef Match::Clock Clock;
    auto at = [](Clock::time_point start, int seconds) { return start + chrono::seconds(seconds); };

    // ---- the text of the clue
    CHECK(cleanClueText("FIRE") == "FIRE");
    CHECK(cleanClueText("  ice   cream \t") == "ice cream");                    // spaces around and between the words are cleaned
    CHECK(cleanClueText("spider-man") == "spider-man");                          // a hyphen does not make two words
    CHECK(rejects([] { cleanClueText(""); }));
    CHECK(rejects([] { cleanClueText("   \t "); }));
    CHECK(rejects([] { cleanClueText("one two three"); }));                      // at most 2 words
    CHECK(cleanClueText("two\nwords") == "two words");                           // a line break is a space
    CHECK(rejects([] { cleanClueText(string("a") + char(7) + "b"); }));          // no other control characters
    CHECK(rejects([] { cleanClueText(string(41, 'x')); }));                      // too long
    CHECK(cleanClueText(string(40, 'x')).size() == 40);

    // ---- who may give it, when, and which number
    {
        Match match(fullLobby(), characters());
        Team team = match.currentTeam(), other = otherThan(team);
        CHECK(!match.hasClue() && match.triesRemaining() == 0 && match.clueHistory().empty());
        try { match.currentClue(); CHECK(false); } catch (const logic_error&) {}
        CHECK(rejects([&] { match.giveClue(guesserOf(team), "FIRE", 3); }));     // only the spy
        CHECK(rejects([&] { match.giveClue(spyOf(other), "FIRE", 3); }));        // only the team on turn
        CHECK(rejects([&] { match.giveClue("nobody", "FIRE", 3); }));
        CHECK(rejects([&] { match.giveClue(spyOf(team), "too many words", 3); }));
        CHECK(rejects([&] { match.giveClue(spyOf(team), "", 3); }));
        CHECK(rejects([&] { match.giveClue(spyOf(team), "FIRE", 0); }));         // from 1...
        CHECK(rejects([&] { match.giveClue(spyOf(team), "FIRE", -2); }));
        CHECK(rejects([&] { match.giveClue(spyOf(team), "FIRE", 9); }));         // ...to the cards the team still has (8)
        CHECK(!match.hasClue());                                                 // rejected clues change nothing
        CHECK(rejects([&] { match.guess(guesserOf(team), 0); }));                // no guessing before the clue

        match.giveClue(spyOf(team), "  FIRE  ", 3);
        CHECK(match.hasClue() && match.currentClue().text == "FIRE" && match.currentClue().number == 3);
        CHECK(match.currentClue().team == team && match.currentClue().bonusTries == 0 && match.currentClue().found == 0);
        CHECK(match.triesRemaining() == 3 && match.clueHistory().size() == 1);
        CHECK(rejects([&] { match.giveClue(spyOf(team), "WATER", 2); }));        // one clue per turn
        CHECK(match.currentClue().text == "FIRE");
        CHECK(match.currentTeam() == team);                                      // giving it does not pass the turn
    }

    // ---- the example: FIRE 3, one good try and then a wrong card; the next clue SUNSHINE 2 has 2 + 2 tries
    {
        Match match(fullLobby(), characters());
        Team team = match.currentTeam(), other = otherThan(team);
        vector<int> mine = cardsWith(match, spyOf(team), keyOf(team));
        vector<int> beige = cardsWith(match, spyOf(team), CardKey::BEIGE);
        vector<int> theirs = cardsWith(match, spyOf(team), keyOf(other));

        match.giveClue(spyOf(team), "FIRE", 3);
        CHECK(match.guess(guesserOf(team), mine[0]) == keyOf(team));
        CHECK(match.triesRemaining() == 2 && match.currentClue().found == 1 && match.currentTeam() == team);
        CHECK(match.guess(guesserOf(team), beige[0]) == CardKey::BEIGE);         // the second try is a beige card
        CHECK(match.currentTeam() == other && !match.hasClue());                 // the turn is skipped
        CHECK(match.pendingExtraTries(team) == 2);                               // 3 cards in the clue, 1 found: 2 are carried over
        CHECK(match.pendingExtraTries(other) == 0);
        CHECK(match.clueHistory()[0].found == 1 && match.clueHistory()[0].number == 3);

        match.giveClue(spyOf(other), "OCEAN", 1);                                // the other team plays and ends its turn
        match.endTurn(guesserOf(other));
        CHECK(match.currentTeam() == team && match.pendingExtraTries(team) == 2); // the carry-over waits for the next clue

        match.giveClue(spyOf(team), "SUNSHINE", 2);
        CHECK(match.triesRemaining() == 4);                                      // 2 for SUNSHINE + 2 from last turn
        CHECK(match.currentClue().number == 2 && match.currentClue().bonusTries == 2);
        CHECK(match.pendingExtraTries(team) == 0);                               // and it is used up
        CHECK(match.clueHistory().size() == 3);
        for (int k = 1; k <= 3; k++) {                                           // three good tries: still the team's turn
            CHECK(match.guess(guesserOf(team), mine[k]) == keyOf(team));
            CHECK(match.currentTeam() == team && match.triesRemaining() == 4 - k);
        }
        CHECK(match.guess(guesserOf(team), mine[4]) == keyOf(team));             // the fourth and last try
        CHECK(match.currentTeam() == other && !match.hasClue());                 // no tries left: the turn passes by itself
        CHECK(match.pendingExtraTries(team) == 0);                               // all good: nothing carried over
        CHECK(match.clueHistory()[2].found == 4);
        CHECK(match.remaining(team) == 8 - 5);                                   // 1 + 4 cards found

        // the other team's card also ends the turn and also carries the unused tries over
        match.giveClue(spyOf(other), "CLUE", 2);
        match.endTurn(guesserOf(other));
        match.giveClue(spyOf(team), "STORM", 3);
        CHECK(match.guess(guesserOf(team), theirs[0]) == keyOf(other));          // first try wrong: nothing found
        CHECK(match.currentTeam() == other && match.pendingExtraTries(team) == 3);
    }

    // ---- the carry-over is the clue number minus the good tries, and never negative
    {
        Match match(fullLobby(), characters());
        Team team = match.currentTeam(), other = otherThan(team);
        vector<int> mine = cardsWith(match, spyOf(team), keyOf(team));
        vector<int> beige = cardsWith(match, spyOf(team), CardKey::BEIGE);
        match.giveClue(spyOf(team), "A", 1);
        match.guess(guesserOf(team), beige[0]);
        CHECK(match.pendingExtraTries(team) == 1);                               // a clue of 1, none found
        match.giveClue(spyOf(other), "B", 1); match.endTurn(guesserOf(other));

        match.giveClue(spyOf(team), "C", 2);                                     // 2 + 1 = 3 tries
        CHECK(match.triesRemaining() == 3);
        match.guess(guesserOf(team), mine[0]);
        match.guess(guesserOf(team), mine[1]);                                   // the 2 cards of the clue
        match.guess(guesserOf(team), beige[1]);                                  // and a wrong third try
        CHECK(match.pendingExtraTries(team) == 0);                               // 2 - 2 = 0: more good tries than the clue number
        match.giveClue(spyOf(other), "D", 1); match.endTurn(guesserOf(other));

        // ending the turn on purpose gives nothing, even with tries left
        match.giveClue(spyOf(team), "E", 3);
        match.guess(guesserOf(team), mine[2]);
        match.endTurn(guesserOf(team));
        CHECK(match.currentTeam() == other && match.pendingExtraTries(team) == 0);
        match.giveClue(spyOf(other), "F", 1); match.endTurn(guesserOf(other));
        match.endTurn(guesserOf(team));                                          // ending before any clue is allowed too
        CHECK(match.currentTeam() == other && match.pendingExtraTries(team) == 0);
    }

    // ---- the number cannot be higher than the cards the team has left
    {
        Match match(fullLobby(), characters());
        Team team = match.currentTeam(), other = otherThan(team);
        vector<int> mine = cardsWith(match, spyOf(team), keyOf(team));
        match.giveClue(spyOf(team), "ALL", 8);
        for (int i = 0; i < 7; i++) match.guess(guesserOf(team), mine[i]);       // 7 found, 8 tries: one left
        CHECK(match.triesRemaining() == 1 && match.remaining(team) == 1);
        match.endTurn(guesserOf(team));
        match.giveClue(spyOf(other), "X", 1); match.endTurn(guesserOf(other));
        CHECK(rejects([&] { match.giveClue(spyOf(team), "MORE", 2); }));         // only 1 card left
        match.giveClue(spyOf(team), "LAST", 1);
        match.guess(guesserOf(team), mine[7]);
        CHECK(match.isFinished() && match.winner() == team);
    }

    // ---- time: running out of time gives no extra tries and wipes the clue
    {
        Lobby timed = fullLobby();
        timed.chooseTimer(true, 30);
        for (string name : {"bs", "bg", "rs", "rg"}) timed.setReady(name, true);
        Clock::time_point start = Clock::now();
        Match match(timed, characters(), start);
        Team team = match.currentTeam(), other = otherThan(team);
        vector<int> mine = cardsWith(match, spyOf(team), keyOf(team));
        match.giveClue(spyOf(team), "SLOW", 4, at(start, 5));
        match.guess(guesserOf(team), mine[0], at(start, 10));
        match.advanceClock(at(start, 30));                                       // the 30 s are up
        CHECK(match.currentTeam() == other && !match.hasClue() && match.triesRemaining() == 0);
        CHECK(match.pendingExtraTries(team) == 0);
        CHECK(match.clueHistory().size() == 1 && match.clueHistory()[0].found == 1);
        CHECK(rejects([&] { match.giveClue(spyOf(team), "LATE", 1, at(start, 31)); }));   // it is not their turn any more
        match.giveClue(spyOf(other), "FAST", 2, at(start, 31));
        // a whole number of periods later it is the same team again, but it is a new turn: a new clue is needed
        match.advanceClock(at(start, 30 + 60));
        CHECK(match.currentTeam() == other && !match.hasClue());
    }

    // ---- the black card ends the match whatever the tries
    {
        Match match(fullLobby(), characters());
        Team team = match.currentTeam();
        match.giveClue(spyOf(team), "RISK", 5);
        match.guess(guesserOf(team), cardsWith(match, spyOf(team), CardKey::BLACK)[0]);
        CHECK(match.isFinished() && match.winner() == otherThan(team));
        CHECK(rejects([&] { match.giveClue(spyOf(otherThan(team)), "AFTER", 1); }));
    }
    { // a clue cannot be given to a match that ended by forfeit
        Match match(fullLobby(), characters());
        match.forfeit("bg");
        CHECK(rejects([&] { match.giveClue(spyOf(match.currentTeam()), "LATE", 1); }));
    }
}

void testGrid() {
    for (int run = 0; run < 500; run++) {
        Grid grid(characters());
        grid.assignCharacters();
        grid.assignKeyCards();
        map<CardKey, int> keys;
        set<string> names;
        for (const Card& card : grid.lineup) {
            keys[card.key]++;
            names.insert(card.characterName);
            CHECK(card.state == CardState::SECRET);
        }
        CHECK(keys[CardKey::BLUE] == 8 && keys[CardKey::RED] == 8 && keys[CardKey::BEIGE] == 8 && keys[CardKey::BLACK] == 1);
        CHECK(names.size() == 25);
    }
    Grid small(characters(10));
    CHECK(rejects([&] { small.assignCharacters(); }));

    Card card;
    card.init(Character("X", M_CharacterType::PERSON, M_Universe::MCU, "images/mcu/x"));
    CHECK(card.imageFor(CardKey::BLUE) == "images/mcu/x/blue.png" && card.imageFor(CardKey::BLACK) == "images/mcu/x/black.png");
}

void testMatchSetupAndViews() {
    Lobby notFull;
    notFull.addPlayer("a", "BLUE", "SPY");
    CHECK(rejects([&] { Match match(notFull, characters()); }));
    CHECK(rejects([&] { Match match(fullLobby(), characters(5)); }));

    Match match(fullLobby(), characters());
    map<CardKey, int> spyKeys, guesserKeys;
    for (const CardView& card : match.viewFor("bs")) {
        spyKeys[card.key]++;
        string file = toString(card.key);
        transform(file.begin(), file.end(), file.begin(), ::tolower);
        CHECK(card.image.size() > file.size() + 4 && card.image.substr(card.image.size() - file.size() - 4) == file + ".png");   // picture matches the key
    }
    for (const CardView& card : match.viewFor("rg")) {
        guesserKeys[card.key]++;
        CHECK(card.state == CardState::SECRET);
        CHECK(card.image.size() > 9 && card.image.substr(card.image.size() - 9) == "beige.png");   // never a colored picture
    }
    CHECK(spyKeys[CardKey::BLUE] == 8 && spyKeys[CardKey::RED] == 8 && spyKeys[CardKey::BEIGE] == 8 && spyKeys[CardKey::BLACK] == 1);
    CHECK(guesserKeys[CardKey::BEIGE] == 25);
    CHECK(match.remaining(Team::BLUE) == 8 && match.remaining(Team::RED) == 8 && !match.isFinished());
    CHECK(rejects([&] { match.viewFor("nobody"); }));
    try { match.winner(); CHECK(false); } catch (const logic_error&) {}
}

void testMatchPermissions() {
    Match match(fullLobby(), characters());
    Team team = match.currentTeam(), other = otherThan(team);
    CHECK(rejects([&] { match.guess(spyOf(team), 0); }));
    CHECK(rejects([&] { match.guess(guesserOf(other), 0); }));
    CHECK(rejects([&] { match.guess(guesserOf(team), 0); }));                  // no clue yet
    clueFor(match, 3);
    CHECK(rejects([&] { match.guess(guesserOf(team), -1); }));
    CHECK(rejects([&] { match.guess(guesserOf(team), 25); }));
    CHECK(rejects([&] { match.guess("nobody", 0); }));
    CHECK(rejects([&] { match.endTurn(spyOf(team)); }));
    CHECK(rejects([&] { match.endTurn(guesserOf(other)); }));

    match.endTurn(guesserOf(team));
    CHECK(match.currentTeam() == other);
    CHECK(rejects([&] { match.endTurn(guesserOf(team)); }));
    match.endTurn(guesserOf(other));
    CHECK(match.currentTeam() == team);
}

void testMatchOutcomes() {
    { // the black card loses on the spot, and nothing works afterwards
        Match match(fullLobby(), characters());
        Team team = match.currentTeam();
        vector<CardView> spy = match.viewFor(spyOf(team));
        int black = -1;
        for (int i = 0; i < 25; i++) if (spy[i].key == CardKey::BLACK) black = i;
        clueFor(match, 1);
        CHECK(match.guess(guesserOf(team), black) == CardKey::BLACK);
        CHECK(match.isFinished() && match.winner() == otherThan(team));
        CHECK(rejects([&] { match.guess(guesserOf(team), 0); }));
        CHECK(rejects([&] { match.endTurn(guesserOf(team)); }));
    }
    { // own cards keep the turn, the last one wins, revealed cards show their key
        Match match(fullLobby(), characters());
        Team team = match.currentTeam();
        vector<CardView> spy = match.viewFor(spyOf(team));
        clueFor(match);                                                          // 8 cards, 8 tries
        int found = 0;
        for (int i = 0; i < 25; i++) {
            if (spy[i].key != keyOf(team)) continue;
            CHECK(match.guess(guesserOf(team), i) == keyOf(team));
            CHECK(rejects([&] { match.guess(guesserOf(team), i); }));            // already revealed
            if (++found < 8) CHECK(match.currentTeam() == team && !match.isFinished());
        }
        CHECK(match.isFinished() && match.winner() == team && match.remaining(team) == 0);
        int revealed = 0;
        for (const CardView& card : match.viewFor(guesserOf(otherThan(team)))) {
            if (card.state == CardState::REVEALED) { revealed++; CHECK(card.key == keyOf(team)); }
        }
        CHECK(revealed == 8);
    }
    { // BEIGE and the other team's card pass the turn
        Match match(fullLobby(), characters());
        Team team = match.currentTeam(), other = otherThan(team);
        vector<CardView> spy = match.viewFor(spyOf(team));
        int beige = -1, theirs = -1, mine = -1;
        for (int i = 0; i < 25; i++) {
            if (spy[i].key == CardKey::BEIGE) beige = i;
            if (spy[i].key == keyOf(other)) theirs = i;
            if (spy[i].key == keyOf(team)) mine = i;
        }
        clueFor(match, 2);
        match.guess(guesserOf(team), beige);
        CHECK(match.currentTeam() == other && !match.isFinished());
        clueFor(match, 3);
        match.guess(guesserOf(other), theirs);                                   // their own card: they keep the turn
        CHECK(match.currentTeam() == other && match.remaining(other) == 7);
        match.guess(guesserOf(other), mine);                                     // the opponent's card: the turn passes
        CHECK(match.currentTeam() == team && match.remaining(team) == 7);
    }
    { // revealing the other team's last card makes THEM win
        Match match(fullLobby(), characters());
        Team team = match.currentTeam(), other = otherThan(team);
        vector<CardView> spy = match.viewFor(spyOf(team));
        vector<int> theirs;
        for (int i = 0; i < 25; i++) if (spy[i].key == keyOf(other)) theirs.push_back(i);
        for (int i = 0; i < 8; i++) {
            Team turn = match.currentTeam();
            if (!match.hasClue()) clueFor(match, 1);
            match.guess(guesserOf(turn), theirs[i]);                             // whoever guesses, these are `other`'s cards
            if (!match.isFinished() && match.currentTeam() == team) {
                if (!match.hasClue()) clueFor(match, 1);
                match.endTurn(guesserOf(team));
            }
        }
        CHECK(match.isFinished() && match.winner() == other);
    }
    { // random games always end, with a winner that matches what happened
        for (int game = 0; game < 2000; game++) {
            Match match(fullLobby(), characters());
            bool black = false;
            Team blackBy = Team::BLUE;
            int moves = 0;
            while (!match.isFinished()) {
                CHECK(++moves < 200);
                if (moves >= 200) break;
                Team team = match.currentTeam();
                vector<CardView> view = match.viewFor(guesserOf(team));
                vector<int> hidden;
                for (int i = 0; i < 25; i++) if (view[i].state == CardState::SECRET) hidden.push_back(i);
                if (!match.hasClue()) clueFor(match, 1 + (int)(rng()() % match.remaining(team)));
                if (rng()() % 5 == 0) { match.endTurn(guesserOf(team)); continue; }
                if (match.guess(guesserOf(team), hidden[rng()() % hidden.size()]) == CardKey::BLACK) { black = true; blackBy = team; }
            }
            if (!match.isFinished()) break;
            if (black) CHECK(match.winner() != blackBy);
            else CHECK(match.remaining(match.winner()) == 0);
        }
    }
}

void testForfeit() {
    // Every player, of either team and either role, can forfeit; the other team wins
    for (string quitter : {"bs", "bg", "rs", "rg"}) {
        Match match(fullLobby(), characters());
        Team team = (quitter[0] == 'b') ? Team::BLUE : Team::RED;
        CHECK(!match.wasForfeited());
        try { match.forfeitedBy(); CHECK(false); } catch (const logic_error&) {}
        match.forfeit(quitter);
        CHECK(match.isFinished() && match.wasForfeited());
        CHECK(match.forfeitedBy() == team && match.winner() == otherThan(team));
    }
    { // it works when it is NOT their team's turn, and the board stays readable
        Match match(fullLobby(), characters());
        Team team = otherThan(match.currentTeam());
        match.forfeit(spyOf(team));
        CHECK(match.winner() == otherThan(team));
        CHECK(match.viewFor("bg").size() == 25);
        CHECK(match.remaining(Team::BLUE) == 8 && match.remaining(Team::RED) == 8);   // no cards were touched
    }
    { // after the end nothing else works, and an unknown player cannot forfeit
        Match match(fullLobby(), characters());
        CHECK(rejects([&] { match.forfeit("nobody"); }));
        CHECK(!match.isFinished());
        match.forfeit("bg");
        CHECK(rejects([&] { match.forfeit("rg"); }));                          // already over: the first forfeit stands
        CHECK(match.forfeitedBy() == Team::BLUE && match.winner() == Team::RED);
        CHECK(rejects([&] { match.guess(guesserOf(match.currentTeam()), 0); }));
        CHECK(rejects([&] { match.endTurn(guesserOf(match.currentTeam())); }));
    }
    { // a match that ends normally is not a forfeit
        Match match(fullLobby(), characters());
        Team team = match.currentTeam();
        vector<CardView> spy = match.viewFor(spyOf(team));
        clueFor(match, 1);
        for (int i = 0; i < 25; i++) if (spy[i].key == CardKey::BLACK) match.guess(guesserOf(team), i);
        CHECK(match.isFinished() && !match.wasForfeited());
    }
}

int main() {
    testEnums();
    testLobby();
    testWaitingRoom();
    testUniverses();
    testTimer();
    testClues();
    testGrid();
    testMatchSetupAndViews();
    testMatchPermissions();
    testMatchOutcomes();
    testForfeit();

    if (failures == 0) cout << "All tests passed" << endl;
    else cout << failures << " check(s) failed" << endl;
    return failures == 0 ? 0 : 1;
}
