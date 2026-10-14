# Online Boardgame

A two-team spy game played on a 5x5 grid of Marvel character cards. Each team (BLUE and RED) has a SPY and a GUESSER. Spies see the colour of every card (BLUE, RED, BEIGE or BLACK); guessers see every card as BEIGE until it is revealed.

## Layout

| Folder | What is in it |
|---|---|
| `src/game/` | The game rules: `Card`, `Grid`, `Player`, `Lobby`, `Match` and the enums. No input or output. |
| `src/db/` | Loads the characters from the SQLite database. |
| `src/console/` | The game played in the terminal. |
| `src/server/` | The HTTP server (JSON API) a front-end talks to. The endpoints are listed at the top of `server.cpp`. |
| `tests/` | Tests for the game rules. |
| `data/` | `characters.db`, the characters table. |
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
2. Each player types a name and picks a free team and role. The game starts when all 4 seats are taken.
3. Spies see the colour of every card. Guessers click the cards of their team on their turn, and press **End turn** when they are done. Clues are given outside the app.
4. A guess ends the turn if the card is the other team's or beige. The black card loses the game. The first team to reveal all its cards wins.

The server runs one game; restart it to play again. To let other devices on your network join, run `build/server 8080 0.0.0.0`.

## Database

`image_path` in the `marvel` table is the character's picture folder as the front-end requests it (for example `images/mcu/iron_man_tony_stark`). The server serves it from `assets/images/`, and the card file is named after the key: `<image_path>/<key>.png`.
