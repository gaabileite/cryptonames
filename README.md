<p align="center"><img src="assets/brand/wordmark-transparent.png" alt="CryptoNames" width="420"></p>

# CryptoNames

**Play it online: https://playcryptonames.cc**

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
| `assets/brand/` | The original logo and wordmark of CryptoNames, `logo-transparent-source.png`, the logo with a transparent background that the tab icons are made from, and `wordmark-transparent-source.png`, the wordmark with a transparent background (`wordmark-transparent.png` is its lighter version, trimmed and 1000 px wide, shown at the top of this README). The web-sized versions and the favicons are in `web/brand/`. |
| `assets/images/` | The four coloured cards of each character: `<universe>/<character>/{blue,red,beige,black}.png`. |
| `third_party/` | Single-header libraries: cpp-httplib and nlohmann/json (both MIT). |
| `Dockerfile`, `fly.toml` | To host the server online (see *Hosting it online*). |
| `web/` | The front-end (plain HTML, CSS and JavaScript), served at `/` by the server. `web/fonts/` has the game's font, Chakra Petch (SIL Open Font License, `OFL.txt` next to it), subset to Latin letters and kept in the repo so the game needs no internet. |

## Build and run

Needs a C++17 compiler and SQLite (both ship with macOS). Run everything from this folder.

```
make            # builds build/game and build/server
make test       # builds and runs the tests
build/game      # play in the terminal (2 to 10 players, one after the other)
build/server    # start the server on http://127.0.0.1:8080
```

`build/server [port] [host]` takes another port (`build/server 9000`) or another host. To let other devices on your network join, run `build/server 8080 0.0.0.0`. When no port is given, the server uses the `PORT` environment variable if there is one (and then listens on `0.0.0.0`), which is how hosting services run it.

Rooms live in the server's memory, so restarting the server closes them all.

## Play in the browser

1. Run `build/server` and open http://127.0.0.1:8080 (every player opens it in their own browser). The round **(i)** button in the top bar (on every page) opens a short guide to how to play, in a dialog in the middle of the screen (close it with the X, Escape or a click outside), and the GitHub symbol next to it links to this repository.
2. Each player types a name on the start page and then either **creates a room** or **joins one with its code**. A new room gets a random code (5 letters and digits, like `K7QD2`); the first player shares it, with the *Copy invite link* button in the waiting room or by just telling it. An invite link (`/?room=K7QD2`) opens the start page with the code already typed. The server can hold many rooms at once, each with its own players, settings and game. A room disappears as soon as its last player leaves it (or when nobody has used it for 2 hours), and rooms are not listed anywhere: you need the code to find one. Once you are in a room, the round arrow at the top left (of the waiting room, and of the pages for joining a game in progress or waiting for the next one) takes you back to the start page and leaves the room; your name and the room code stay typed in, so you can try again.
3. The waiting room keeps the teams on the left and the settings (universes, time limits) on the right, with the **I'm ready** button below the time limits, so a computer screen needs no scrolling, and it only shows a short message when something stops the game from starting (for example "The Red team needs a spy."). Everybody picks a team and a role (Blue or Red, Spy or Guesser) and can change it at any time. A room takes from 2 to 10 players, and any number of them can share a team and a role. With 4 or more players each team needs at least one spy and one guesser (the waiting room says what is missing). Whenever a player joins or leaves, everybody's ready is cancelled, so nobody starts a game with players they did not agree to.
4. The waiting room also has a **Universes** picker: it decides which universes the 25 cards of the game are drawn from (each shows how many characters it has). Anybody in the waiting room can switch a universe on or off (unless the host turned *All can edit* off, see below), and doing so cancels everybody's ready. At least 25 characters must stay selected: switching off a universe that would leave too few does nothing and a short message says how many would be left. A new room starts with every universe selected.
5. The waiting room also has two optional **time limits**, each with its own checkbox (the seconds, from 15 to 600, and the quick choices only show once it is ticked): one for the **spy to give the clue**, counted from the start of the turn, and one for the **guesser**, counted from the moment the clue is sent. During the match everybody sees a countdown that says which one it is ("Clue ⏱ 0:42" or "Guessing ⏱ 0:42") and turns red for the last 10 seconds. When time runs out, the turn passes to the other team by itself and nothing is carried over. Guessing right does not add time. The server keeps the clock, so nobody's browser can change it. Anybody can change the limits (unless the host turned *All can edit* off), and doing so cancels everybody's ready.
6. When you are happy with your seat, press **I'm ready**. Pressing it again cancels it, and your seat is locked while you are ready. The game starts by itself when there are at least 2 players, nothing is missing and everybody is ready.
7. Each turn starts with a **clue**. A spy of the team on turn types 1 or 2 words and picks a number (from 1 to the cards the team still has to find). Everybody sees the clue (and every clue given so far is listed in the *Clues* panel at the side of the board, or below it on a small screen). Only then can the team's guessers click cards (any of them), and the number is how many **tries** the team has: every card revealed costs one try, and the turn ends by itself when they run out. Spies see the colour of every card, guessers see beige until a card is revealed.
8. If the guesser reveals a **wrong card** (beige or the other team's), the turn ends at once, and the tries that were not used well are added to the team's next clue: the clue number minus the good tries. Example: clue FIRE 3, one good card and then a beige one: the next clue, SUNSHINE 2, gives the guesser 2 + 2 = 4 tries. Ending the turn on purpose, or running out of time, carries nothing over.
9. Any player of either team (spy or guesser) can press **Forfeit** at any time, even when it is not their turn. A second click confirms it, and then the other team wins (in a co-operative game there is no other team, so the game is simply lost).
10. **Leave the room** takes you back to the start page. During a match you are asked first: your team goes on without you, unless you were its last spy or its last guesser, and then it forfeits. After the match leaving is free. When the last player has left, the room is deleted. To play again with the same people, use *Play again* (see below) instead of leaving.
11. The black card loses the game for the team that reveals it. The first team to reveal all its cards wins. Every card that is picked turns over on every player's screen.

### The spy's notepad

Spies have a **Notepad** on the other side of the board from the *Clues* panel (on a wide screen: notepad, board, clues; on a smaller one it goes below the clues). It is for **future clues**, in the same format as a clue: 1 or 2 words and a number (from 1 to the cards the team still has to find).

- Type the words, pick the number and press *Add*. Newest notes are on top; the **×** deletes one.
- **Click a note** to put it in the clue box (the staging area where you write your clue): the words and the number are filled in, and it is **not** sent until you press *Send clue*. You can do it before your turn: the clue will be waiting in the box when it comes. **When you send that clue, its note is deleted** (it is used up). If you change the words before sending, the note stays; and a clue you type by hand with exactly the words and the number of a note uses that note up as well. Only staging a note, without sending, changes nothing.
- **Notes are private.** They are kept only in your own browser tab (never sent to the server), so nobody else, not even your teammates, can see them. A page refresh keeps them; a new game starts with an empty notepad. The notepad holds up to 30 notes.
- Guessers do not have a notepad.

### Pins

Every card has a **pin** button at its top right: a reminder for your team, *"I want to pick this one later"*. Clicking the pin only highlights the card; clicking the card itself is what picks it.

- **Everybody sees every pin, and the colour says who put it:** a blue pin is the blue team's and a red pin the red team's (in a co-operative game everything is blue). Your own team's pin is the button at the top right of the card; if the other team pinned the same card too, their pin is drawn next to yours and the card gets a ring in each team's colour. So the other team can see what you plan to pick.
- A pin belongs to the **team**: any player of the team, spy or guesser, can add it or take it off.
- A card that gets picked loses its pins. A refresh keeps the pins (the server holds them), and a new game starts without any.
- Pinning changes nothing in the game itself (it does not use a try or pass the turn).

### The host and who can edit the room

The player who makes a room is its **host** (`creator` on the player; the lobby calls it `Member.creator`, and `Player.creator` carries it into the match). Only the host sees a switch in the waiting room, **All can edit**:

- **On (the default):** any player can change the room: the universes, the time limits and **the seats of the other players**. To move somebody, pick their name in the *Players* row (click it again to let go) and then pick a seat. Moving a player who was ready cancels their ready.
- **Off:** only the host can do those things. For everybody else the universes and the time limits are dimmed and cannot be clicked, and the *Players* row is hidden. The server enforces this too (it answers 403), so it cannot be bypassed from the browser's console.
- **Always allowed:** choosing your own seat, pressing ready and leaving. The switch does not matter once the match starts.
- If the host leaves, the player who has been in the room the longest becomes the host (with the same switch setting). *Play again* keeps the host and the setting.

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

## Hosting it online

GitHub Pages cannot host this game (it only serves static files and the game needs the server). Use a host that runs containers; the `Dockerfile` works on any of them (Fly.io, Railway, Render, or a VPS with Docker). The server reads the `PORT` variable the host gives it (and then listens on `0.0.0.0`).

Rooms live in the server's memory, so run **exactly one** copy and never let it sleep or autoscale: a second copy would be a separate world, and a stop or a redeploy closes every room.

To try the image on your computer: `docker build -t cryptonames .` and `docker run -p 8080:8080 cryptonames`, then open http://localhost:8080.

### The domain

`playcryptonames.cc` is registered at Cloudflare, which also hosts its DNS. The records, all set to *DNS only* (grey cloud, so Fly can issue the HTTPS certificate), are `A` and `AAAA` for `@` and for `www`, pointing at the app's addresses (`fly ips list`). The certificates are made with `fly certs add playcryptonames.cc` and `fly certs add www.playcryptonames.cc` (`fly certs list` shows their state).

### Fly.io (region: US East, Virginia)

The game is deployed this way at https://playcryptonames.cc (the Fly app is called `cryptonames`, so it also answers at cryptonames.fly.dev). A Fly trial account stops machines after 5 minutes: add a payment method on the account (https://fly.io/trial) or the game goes offline.

`fly.toml` is already set up for one always-on machine in Virginia (`iad`). In the Fly dashboard use **Apps**, not Sprites (Sprites is a different product). From this folder:

```
brew install flyctl                 # the Fly command line (macOS)
fly auth login                      # opens the browser; Fly needs a payment method on the account
fly launch --no-deploy --copy-config   # keeps fly.toml; if the app name "cryptonames" is taken, pick another
fly deploy --ha=false               # builds the image on Fly and starts ONE machine
fly open                            # opens the game's address (https://<app>.fly.dev)
```

To update after a change: `fly deploy --ha=false` again (this closes the open rooms). `fly logs` shows what the server prints, and `fly scale count 1` makes sure there is only one machine.

The game's address, `https://playcryptonames.cc`, appears in `web/index.html` (the canonical link and the share tags), `web/robots.txt`, `web/sitemap.xml` and as `CANONICAL_HOST` in `fly.toml`: change all of them if the game gets another address. With `CANONICAL_HOST` set, a page request that arrives through `www.playcryptonames.cc` or `cryptonames.fly.dev` is redirected (permanently, keeping the path and the `?room=` invite code) to the main address, so search engines see one site; `/healthz` and `/api/` are never redirected. The Fly command line may rewrite `fly.toml` when it deploys, so look at `git diff` before committing it. `web/google47bc8078b02ff9fb.html` is the file Google Search Console uses to confirm the site is yours; keep it. `web/brand/share.png` is the picture shown when a link to the game is shared.

For players far from Virginia, put Cloudflare (free plan) in front of the app's domain: it caches the page and the card pictures near each player.

## Good to know

- **English only.** The interface and the character names are in English.
- **Names and clues are limited in bytes, not letters.** A player name can be 30 bytes and a clue 40, so a name in a script with multi-byte letters (Japanese, Arabic, Russian...) fits fewer letters. The font only has Latin letters; other scripts show in the system font.
- **A clue has at most 2 words, counted by spaces**, which does not suit languages written without spaces.
- **No accounts or passwords.** Anybody who has a room code can enter the room. A server holds at most 200 rooms, and a room is forgotten after 2 hours without use.
- **The server clock is the only clock.** The timers count on the server, so it does not matter where the players are or what time their devices show; the countdown on screen can be off by about a second.

## Database

`image_path` in the `marvel` table is the character's picture folder as the front-end requests it (for example `images/mcu/iron_man_tony_stark`). The server serves it from `assets/images/`, and the card file is named after the key: `<image_path>/<key>.png`.
