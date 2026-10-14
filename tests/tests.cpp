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
        CHECK(match.guess(guesserOf(team), black) == CardKey::BLACK);
        CHECK(match.isFinished() && match.winner() == otherThan(team));
        CHECK(rejects([&] { match.guess(guesserOf(team), 0); }));
        CHECK(rejects([&] { match.endTurn(guesserOf(team)); }));
    }
    { // own cards keep the turn, the last one wins, revealed cards show their key
        Match match(fullLobby(), characters());
        Team team = match.currentTeam();
        vector<CardView> spy = match.viewFor(spyOf(team));
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
        match.guess(guesserOf(team), beige);
        CHECK(match.currentTeam() == other && !match.isFinished());
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
            match.guess(guesserOf(turn), theirs[i]);                             // whoever guesses, these are `other`'s cards
            if (!match.isFinished() && match.currentTeam() == team) match.endTurn(guesserOf(team));
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
                if (rng()() % 5 == 0) { match.endTurn(guesserOf(team)); continue; }
                if (match.guess(guesserOf(team), hidden[rng()() % hidden.size()]) == CardKey::BLACK) { black = true; blackBy = team; }
            }
            if (!match.isFinished()) break;
            if (black) CHECK(match.winner() != blackBy);
            else CHECK(match.remaining(match.winner()) == 0);
        }
    }
}

int main() {
    testEnums();
    testLobby();
    testWaitingRoom();
    testGrid();
    testMatchSetupAndViews();
    testMatchPermissions();
    testMatchOutcomes();

    if (failures == 0) cout << "All tests passed" << endl;
    else cout << failures << " check(s) failed" << endl;
    return failures == 0 ? 0 : 1;
}
