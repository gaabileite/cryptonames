<p align="center"><img src="web/brand/wordmark.png" alt="CryptoNames" width="420"></p>

# CryptoNames

A spy game played on a 5x5 grid of Marvel character cards, with 2 to 10 players. With 4 or more players it is BLUE against RED, and each team has at least one SPY and one GUESSER (a team can have several of each). With only 2 or 3 players it is a **co-operative game** (see below). Spies see the colour of every card (BLUE, RED, BEIGE or BLACK); guessers see every card as BEIGE until it is revealed.

## Layout

| Folder | What is in it |
|---|---|
| `src/game/` | The game rules: `Card`, `Grid`, `Player`, `Lobby`, `Match`, `Clue`, `Room`, `RoomRegistry` and the enums. No input or output. |
| `src/db/` | Loads the characters from the SQLite database. |
| `src/console/` | The game played in the terminal. |
| `src/server/` | The HTTP server (JSON API) a front-end talks to. The endpoints are listed at the top of `server.cpp`. |
| `tests/` | Tests for the game rules. |
| `data/` | `characters.db`, the characters table. |
| `assets/brand/` | The original logo and wordmark of CryptoNames. The web-sized versions and the favicons are in `web/brand/`. |
| `assets/images/` | The four coloured cards of each character: `<universe>/<character>/{blue,red,beige,black}.png`. |
| `third_party/` | Single-header libraries: cpp-httplib and nlohmann/json (both MIT). |
| `web/` | The front-end (plain HTML, CSS and JavaScript), served at `/` by the server. |

## Build and run

Needs a C++17 compiler and SQLite (both ship with macOS). Run everything from this folder.

```
make            # builds build/game and build/server
make test       # builds and runs the tests
build/game      # play in the terminal
build/server    # start the server on http://127.0.0.1:8080
```

## Play in the browser

1. Run `build/server` and open http://127.0.0.1:8080 (every player opens it in their own browser).
2. Each player types a name on the start page and then either **creates a room** or **joins one with its code**. A new room gets a random code (5 letters and digits, like `K7QD2`); the first player shares it, with the *Copy invite link* button in the waiting room or by just telling it. An invite link (`/?room=K7QD2`) opens the start page with the code already typed. The server can hold many rooms at once, each with its own players, settings and game. A room disappears as soon as its last player leaves it (or when nobody has used it for 2 hours), and rooms are not listed anywhere: you need the code to find one.
3. In the waiting room everybody picks a team and a role (Blue or Red, Spy or Guesser) and can change it at any time. A room takes from 2 to 10 players, and any number of them can share a team and a role. With 4 or more players each team needs at least one spy and one guesser (the waiting room says what is missing). Whenever a player joins or leaves, everybody's ready is cancelled, so nobody starts a game with players they did not agree to.
4. The waiting room also has a **Universes** picker: it decides which universes the 25 cards of the game are drawn from (each shows how many characters it has). Anybody in the waiting room can switch a universe on or off, and doing so cancels everybody's ready. At least 25 characters must stay selected, so a universe that would leave too few is locked. When the room empties, every universe is selected again.
5. The waiting room also has two optional **time limits**, each with its own switch and seconds (from 15 to 600; type it or use the quick choices): one for the **spy to give the clue**, counted from the start of the turn, and one for the **guesser**, counted from the moment the clue is sent. During the match everybody sees a countdown that says which one it is ("Clue ⏱ 0:42" or "Guessing ⏱ 0:42") and turns red for the last 10 seconds. When time runs out, the turn passes to the other team by itself and nothing is carried over. Guessing right does not add time. The server keeps the clock, so nobody's browser can change it. Anybody can change the limits, and doing so cancels everybody's ready.
6. When you are happy with your seat, press **I'm ready**. Pressing it again cancels it, and your seat is locked while you are ready. The game starts by itself when there are at least 2 players, nothing is missing and everybody is ready.
7. Each turn starts with a **clue**. A spy of the team on turn types 1 or 2 words and picks a number (from 1 to the cards the team still has to find). Everybody sees the clue. Only then can the team's guessers click cards (any of them), and the number is how many **tries** the team has: every card revealed costs one try, and the turn ends by itself when they run out. Spies see the colour of every card, guessers see beige until a card is revealed.
8. If the guesser reveals a **wrong card** (beige or the other team's), the turn ends at once, and the tries that were not used well are added to the team's next clue: the clue number minus the good tries. Example: clue FIRE 3, one good card and then a beige one: the next clue, SUNSHINE 2, gives the guesser 2 + 2 = 4 tries. Ending the turn on purpose, or running out of time, carries nothing over.
9. Any player of either team (spy or guesser) can press **Forfeit** at any time, even when it is not their turn. A second click confirms it, and then the other team wins.
10. **Leave the room** takes you back to the start page. During a match you are asked first: your team goes on without you, unless you were its last spy or its last guesser, and then it forfeits. After the match leaving is free. When the last player has left, the room is deleted. To play again with the same people, use *Play again* (see below) instead of leaving.
11. The black card loses the game for the team that reveals it. The first team to reveal all its cards wins. Every card that is picked turns over on every player's screen.

### Joining a game in progress

Anybody can enter a room (with its code or invite link) while the match is going on, as long as the room has fewer than 10 players. Nobody is let in without choosing first: a message says that the room is already playing a game, what kind of game it is (co-operative or Blue against Red, and with how many players), and offers two choices (plus *Cancel*, which leaves nothing behind):

- **Join the game anyway.** The newcomer lands on a **Join the game in progress** page that shows who holds each seat, and has to pick a team (Blue or Red; in a co-operative game everybody is on Blue, so only the role is chosen) and a role (spy or guesser). Until they do, they cannot play and do not see the board; the seat is final for the rest of the game. That page also has *Wait for the next game instead*. Nobody else is interrupted, and joining does not cancel anybody's ready or touch the clocks.
- **Stay in the waiting room.** The newcomer waits in the room's waiting room: they see who is playing, and can still press *Join the game in progress* if they change their mind. When the game is over, they (or anybody in the room) can open the waiting room for a new game.

Other things to know:

- A game that started co-operative stays co-operative, and a newcomer there is always on the Blue team, whatever team they click (they only choose the role).
- A newcomer who picks a spy sees every colour, so a team may want to agree on who takes that role.
- Somebody who left can come back with the same name and picks a new seat.
- A room that enters a finished game has nothing to join: the newcomer simply waits for the next game.

### Playing again

When a match is over, every player (and everybody who was waiting) has a button to go on: **Play again** on the game page, or **Open the waiting room for a new game** for those who were waiting. Anybody can press it, and then the whole room goes back to the waiting room: nobody has a seat or is ready, and the universes and time limits stay as they were. Players who want to leave simply press *Leave the room* instead. The room keeps its code and its players, so there is no need to make a new one.

### Co-operative game (2 or 3 players)

With fewer than 4 players there is no opposing team. When the game starts, an alert says *"Your game only contains two people. This will be a co-operative game."* and you press **OK**.

- Everybody plays on the **Blue** team, whatever team they picked in the waiting room. The roles they picked are kept; if everybody picked the same role, one player, chosen at random, takes the other (so there is always at least one spy and one guesser).
- It is always Blue's turn: a clue, then guesses. A wrong card ends the turn (and the unused tries carry over, as usual) and the spy gives a new clue.
- A Red or beige card is a wrong card. The team wins by finding **all the Blue cards**, and loses when it reveals the black card or forfeits. Revealing every Red card does not win anything.
- The time limits work the same way; when time runs out the turn starts over for Blue.

Rooms live in the server's memory, so restarting the server closes them all. To let other devices on your network join, run `build/server 8080 0.0.0.0`.

## Database

`image_path` in the `marvel` table is the character's picture folder as the front-end requests it (for example `images/mcu/iron_man_tony_stark`). The server serves it from `assets/images/`, and the card file is named after the key: `<image_path>/<key>.png`.
