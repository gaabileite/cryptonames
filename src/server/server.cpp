// HTTP server for CryptoNames: many rooms at once, each with its own waiting room and match.
//
// Build: make server
// Run:   build/server [port] [host]      (defaults: 8080 127.0.0.1) - run it from the project folder.
//
// Every endpoint answers JSON. A rejected action answers {"error": "..."} with a 4xx status.
// A room has a random code. A player makes a room or enters one with its code, and gets a secret token; from then on
// the token, sent in the X-Token header, identifies both the player and the room, so every other endpoint is the same
// for all rooms. A room disappears when its last player leaves it (or when nobody has used it for 2 hours).
//
// Entering:
//   POST /api/rooms/create  {"name"} makes a room and puts you in it -> {"code", "token", ...}
//   POST /api/rooms/join    {"code", "name", "duringMatch": "play"|"wait"} -> {"code", "token", ...}   404 if there is no such room
//                           While a match is going on, "duringMatch" is required: without it the answer is 409 with
//                           {"error", "matchInProgress": true, "cooperative", "players"} and nobody is let in, so the
//                           page can ask the player.
//                           "play": the newcomer has no team and no role until they pick them with /api/seat, once, and
//                           cannot play before that. "wait": they stay in the room's waiting room for the next game
//                           ("waiting": true). Once the match is over nobody has anything to join: they just wait.
// Waiting room (until every seat is taken and everybody is ready, then the match starts by itself):
//   GET  /api/lobby      the room code, who is in the waiting room (team and role are null until picked, plus "ready"),
//                        how many players a game takes (2 to 10), whether the game would be co-operative (fewer than 4
//                        players), what is "missing" before a match could start (null when nothing), the universes
//                        with their character count and whether they are selected, and the time limits   (X-Token)
//   POST /api/seat       {"team", "role"} picks or changes your seat; any number of players can take the same one;
//                        not while ready. During a match only a newcomer without a seat can use it, once (in a
//                        co-operative game the answer shows the team they really got: BLUE)   (X-Token)
//   POST /api/universes  {"universe": "MCU", "selected": true|false} switches one universe on or off; cancels everybody's ready (X-Token)
//   POST /api/timer      {"phase": "clue"|"guess", "enabled": true|false, "seconds": 15-600} the time limit of the clue or
//                        of the guessing; cancels everybody's ready                       (X-Token)
//   POST /api/ready      {"ready": true|false}                                             (X-Token)
//   POST /api/leave      leaves the room (see below)                                       (X-Token)
//   POST /api/wait       {"wait": true|false} during a match, somebody without a seat decides to wait for the next game
//                        (true) or to join this one (false)                                (X-Token)
//   POST /api/play-again after a match is over: everybody in the room goes back to the waiting room, with no seats and
//                        nobody ready, to start another match                              (X-Token)
// Match:
//   GET  /api/state      the room code, which match of the room this is ("game"), the turn, the cards left, the time left, the clue and tries left, the winner,
//                        who you are, whether the game is co-operative, and whether leaving would make your team
//                        forfeit                                                            (X-Token)
//   GET  /api/board      the 25 cards as YOU may see them                                  (X-Token)
//   POST /api/clue       {"text": "1-2 words", "number": n} the SPY's clue of the turn     (X-Token)
//   POST /api/guess      {"index": 0-24} -> {"key"}   only after the clue; every guess is a try   (X-Token)
//   POST /api/end-turn                                                                     (X-Token)
//   POST /api/forfeit    your team gives up (any of its players, any time): the other team wins   (X-Token)
// Leaving: in the waiting room it just frees your name and seat. During a match your team goes on without you, unless
// it is left without a spy or without a guesser: then it forfeits (so nobody waits for a team that cannot play). After
// the match it changes nothing. When the last player has left, the room is gone.
// A game with fewer than 4 players is co-operative: everybody plays on the BLUE team (the roles they picked are kept; if
// they all picked the same one, a random player takes the other), and the team wins by finding all the BLUE cards.
// /images/... serves assets/images/ (the card pictures) and, if a web/ folder exists, / serves the front-end.
#include <algorithm>
#include <iostream>
#include <mutex>
#include <functional>
#include <filesystem>
#include "httplib.h"
#include "json.hpp"
#include "db/database.h"
#include "game/rooms.h"

using namespace std;
using json = nlohmann::json;

// An error with the HTTP status it should be answered with.
struct HttpError : runtime_error {
    int status;
    HttpError(int status, const string& message) : runtime_error(message), status(status) {}
};

// Everything the server knows lives here. Requests run on several threads, so every handler holds the lock.
mutex serverLock;
unique_ptr<Catalog> catalog;
unique_ptr<RoomRegistry> rooms;

// The player who sent the request, and the room they are in, found through the X-Token header.
struct Visitor {
    Room& room;
    string token;
    const Member& you;
};

Visitor visitorOf(const httplib::Request& req) {
    string token = req.get_header_value("X-Token");
    Room& room = rooms->roomOf(token);               // throws NotInRoom
    return Visitor{room, token, room.member(token)};
}

json bodyOf(const httplib::Request& req) {
    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        throw HttpError(400, "The request body must be a JSON object");
    }
    return body;
}

string textField(const json& body, const string& field) {
    if (!body.contains(field) || !body[field].is_string()) {
        throw HttpError(400, "Missing text field: " + field);
    }
    return body[field];
}

bool booleanField(const json& body, const string& field) {
    if (!body.contains(field) || !body[field].is_boolean()) {
        throw HttpError(400, "Missing true/false field: " + field);
    }
    return body[field];
}

// team and role are null while the player has not picked a seat.
json memberJson(const Member& member) {
    json answer = {{"name", member.name}, {"team", nullptr}, {"role", nullptr}, {"ready", member.ready}, {"waiting", member.waiting}};
    if (member.seat) {
        answer["team"] = toString(member.seat->first);
        answer["role"] = toString(member.seat->second);
    }
    return answer;
}

json clueJson(const Clue& clue) {
    return {{"team", toString(clue.team)}, {"text", clue.text}, {"number", clue.number},
            {"bonusTries", clue.bonusTries}, {"found", clue.found}};
}

json timersJson(const Room& room) {
    json answer = {{"min", Lobby::minTurnSeconds}, {"max", Lobby::maxTurnSeconds}};
    for (TimerPhase phase : {TimerPhase::CLUE, TimerPhase::GUESS}) {
        const TimeLimit& limit = room.waitingRoom().timer(phase);
        answer[phase == TimerPhase::CLUE ? "clue" : "guess"] = {{"enabled", limit.enabled}, {"seconds", limit.seconds}};
    }
    return answer;
}

json universesJson(const Room& room) {
    // The biggest universe first
    const map<M_Universe, int>& sizes = room.characters().universeSizes;
    vector<pair<M_Universe, int>> sorted(sizes.begin(), sizes.end());
    stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    json list = json::array();
    for (const auto& universe : sorted) {
        list.push_back({{"name", toString(universe.first)}, {"count", universe.second},
                        {"selected", room.waitingRoom().universes().count(universe.first) > 0}});
    }
    return list;
}

// What a player gets when they enter a room.
json enteredJson(const RoomRegistry::Entry& entry) {
    json answer = memberJson(entry.room->member(entry.token));
    answer["token"] = entry.token;
    answer["code"] = entry.room->code();
    return answer;
}

// Wraps an action: takes the lock, answers its JSON, and turns exceptions into error answers.
httplib::Server::Handler endpoint(function<json(const httplib::Request&)> action) {
    return [action](const httplib::Request& req, httplib::Response& res) {
        lock_guard<mutex> guard(serverLock);
        int status = 200;
        json answer;
        try {
            answer = action(req);
        } catch (const HttpError& e) {
            status = e.status;
            answer = {{"error", e.what()}};
        } catch (const NotInRoom& e) {
            status = 401;
            answer = {{"error", e.what()}};
        } catch (const MatchInProgress& e) {
            status = 409;
            answer = {{"error", e.what()}, {"matchInProgress", true}, {"cooperative", e.cooperative}, {"players", e.players}};
        } catch (const NoSuchRoom& e) {
            status = 404;
            answer = {{"error", e.what()}};
        } catch (const WrongStage& e) {
            status = 409;
            answer = {{"error", e.what()}};
        } catch (const RoomLimit& e) {
            status = 503;
            answer = {{"error", e.what()}};
        } catch (const invalid_argument& e) {
            status = 400;
            answer = {{"error", e.what()}};
        } catch (const exception& e) {
            status = 500;
            answer = {{"error", e.what()}};
        }
        res.status = status;
        res.set_content(answer.dump(), "application/json");
    };
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        try {
            port = stoi(argv[1]);
        } catch (const exception&) {
            port = -1;
        }
        if (port < 1 || port > 65535) {
            cerr << "Error: invalid port " << argv[1] << endl;
            return 1;
        }
    }
    string host = argc > 2 ? argv[2] : "127.0.0.1";

    try {
        catalog = make_unique<Catalog>(loadCharacters(defaultDatabasePath));
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    // Checked now because a match is created when the last player is ready, and it needs 25 characters to fill the board.
    if (catalog->characters.size() < Catalog::cardsOnBoard) {
        cerr << "Error: the database needs at least " << Catalog::cardsOnBoard << " characters, found " << catalog->characters.size() << endl;
        return 1;
    }
    rooms = make_unique<RoomRegistry>(*catalog);

    httplib::Server server;

    // Each browser keeps several connections open, and every open connection holds a worker thread.
    // With the default (small) pool, the images and the polls of a few players wait seconds in line.
    server.new_task_queue = [] { return new httplib::ThreadPool(64); };
    server.set_keep_alive_timeout(2);

    // ---- entering a room

    server.Post("/api/rooms/create", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        return enteredJson(rooms->create(textField(body, "name")));
    }));

    server.Post("/api/rooms/join", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        string code = textField(body, "code");
        string name = textField(body, "name");
        JoinChoice choice = JoinChoice::UNDECIDED;           // only matters when a match is going on
        if (body.contains("duringMatch")) {
            string said = body["duringMatch"].is_string() ? body["duringMatch"].get<string>() : "";
            if (said == "play") choice = JoinChoice::PLAY;
            else if (said == "wait") choice = JoinChoice::WAIT;
            else throw HttpError(400, "duringMatch must be play or wait");
        }
        return enteredJson(rooms->join(code, name, RoomRegistry::Clock::now(), choice));
    }));

    server.Post("/api/wait", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        Visitor in = visitorOf(req);
        in.room.setWaiting(in.token, booleanField(body, "wait"));
        return memberJson(in.room.member(in.token));
    }));

    server.Post("/api/play-again", endpoint([](const httplib::Request& req) {
        Visitor in = visitorOf(req);
        in.room.newGame(in.token);
        return json{{"ok", true}};
    }));

    server.Post("/api/leave", endpoint([](const httplib::Request& req) {
        rooms->leave(req.get_header_value("X-Token"));
        return json{{"ok", true}};
    }));

    // ---- the waiting room

    server.Get("/api/lobby", endpoint([](const httplib::Request& req) {
        Visitor in = visitorOf(req);
        const Lobby& lobby = in.room.waitingRoom();
        json players = json::array();
        for (const Member& member : lobby.members()) {
            players.push_back(memberJson(member));
        }
        string missing = lobby.missing();
        return json{{"code", in.room.code()}, {"started", in.room.started()},
                    {"finished", in.room.started() && in.room.match().isFinished()},
                    {"minPlayers", Lobby::minPlayers}, {"maxPlayers", Lobby::maxPlayers},
                    {"teamGameMinPlayers", Lobby::teamGameMinPlayers}, {"cooperative", lobby.cooperative()},
                    {"missing", missing.empty() ? json(nullptr) : json(missing)}, {"players", players},
                    {"universes", universesJson(in.room)},
                    {"charactersSelected", in.room.characters().charactersIn(lobby.universes())},
                    {"charactersNeeded", Catalog::cardsOnBoard}, {"timers", timersJson(in.room)}};
    }));

    server.Post("/api/seat", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        Visitor in = visitorOf(req);
        in.room.chooseSeat(in.token, teamFromString(textField(body, "team")), roleFromString(textField(body, "role")));
        return memberJson(in.room.member(in.token));
    }));

    server.Post("/api/universes", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        Visitor in = visitorOf(req);
        M_Universe universe = universeFromString(textField(body, "universe"));      // throws if it does not exist
        in.room.setUniverse(in.token, universe, booleanField(body, "selected"));
        return json{{"universes", universesJson(in.room)},
                    {"charactersSelected", in.room.characters().charactersIn(in.room.waitingRoom().universes())},
                    {"charactersNeeded", Catalog::cardsOnBoard}};
    }));

    server.Post("/api/timer", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        Visitor in = visitorOf(req);
        string phaseName = textField(body, "phase");
        if (phaseName != "clue" && phaseName != "guess") {
            throw HttpError(400, "The phase must be clue or guess");
        }
        bool enabled = booleanField(body, "enabled");
        optional<long long> seconds;                     // not sent: the room keeps the seconds that were chosen
        if (body.contains("seconds")) {
            if (!body["seconds"].is_number_integer()) {
                throw HttpError(400, "The seconds must be a whole number");
            }
            seconds = body["seconds"].get<long long>();
        }
        in.room.setTimer(in.token, phaseName == "clue" ? TimerPhase::CLUE : TimerPhase::GUESS, enabled, seconds);
        return json{{"timers", timersJson(in.room)}};
    }));

    server.Post("/api/ready", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        Visitor in = visitorOf(req);
        in.room.setReady(in.token, booleanField(body, "ready"));      // the match starts when everybody is ready
        json answer = memberJson(in.room.member(in.token));
        answer["started"] = in.room.started();
        return answer;
    }));

    // ---- the match

    server.Get("/api/state", endpoint([](const httplib::Request& req) {
        Visitor in = visitorOf(req);
        const Member& you = in.you;
        json answer = {{"room", in.room.code()}, {"game", in.room.gameNumber()}, {"started", in.room.started()}, {"you", memberJson(you)}};
        if (in.room.started()) {
            Match& match = in.room.match();               // brings the clock up to date first
            answer["finished"] = match.isFinished();
            answer["cooperative"] = match.isCooperative();
            answer["playerCount"] = match.playing();
            answer["leavingForfeits"] = you.seat && match.leavingForfeits(you.name);     // a newcomer without a seat plays for nobody
            // The countdown of the phase the turn is in: giving the clue, or guessing after it.
            answer["timer"] = {{"phase", match.hasClue() ? "GUESS" : "CLUE"}, {"enabled", match.hasTimeLimit()},
                               {"seconds", match.phaseLimitSeconds()},
                               {"millisecondsLeft", match.hasTimeLimit() && !match.isFinished() ? json(match.millisecondsLeft()) : json(nullptr)},
                               {"clueSeconds", match.clueLimitSeconds()}, {"guessSeconds", match.guessLimitSeconds()}};
            answer["currentTeam"] = toString(match.currentTeam());
            answer["remaining"] = {{"BLUE", match.remaining(Team::BLUE)}, {"RED", match.remaining(Team::RED)}};
            bool onTurn = you.seat && !match.isFinished() && you.seat->first == match.currentTeam();
            answer["yourTurn"] = onTurn && you.seat->second == Role::GUESSER;                       // the guesser of the team on turn
            answer["canGiveClue"] = onTurn && you.seat->second == Role::SPY && !match.hasClue();
            answer["canGuess"] = onTurn && you.seat->second == Role::GUESSER && match.hasClue();
            json current = nullptr;
            if (!match.isFinished() && match.hasClue()) {
                current = clueJson(match.currentClue());
                current["triesLeft"] = match.triesRemaining();
            }
            answer["clue"] = current;
            json history = json::array();
            for (const Clue& clue : match.clueHistory()) history.push_back(clueJson(clue));
            answer["clues"] = history;
            answer["extraTries"] = {{"BLUE", match.pendingExtraTries(Team::BLUE)}, {"RED", match.pendingExtraTries(Team::RED)}};
            if (match.isFinished()) {
                answer["winner"] = toString(match.winner());
                answer["forfeitedBy"] = match.wasForfeited() ? json(toString(match.forfeitedBy())) : json(nullptr);
            }
        }
        return answer;
    }));

    server.Get("/api/board", endpoint([](const httplib::Request& req) {
        Visitor in = visitorOf(req);
        if (in.room.started() && !in.you.seat) {
            throw HttpError(409, "Pick a team and a role to join the game first");
        }
        json cards = json::array();
        int index = 0;
        for (const CardView& card : in.room.match().viewFor(in.you.name)) {
            cards.push_back({{"index", index++}, {"name", card.characterName}, {"image", card.image},
                             {"key", toString(card.key)}, {"state", toString(card.state)}});
        }
        return json{{"cards", cards}};
    }));

    server.Post("/api/clue", endpoint([](const httplib::Request& req) {
        Visitor in = visitorOf(req);
        json body = bodyOf(req);
        string text = textField(body, "text");
        if (!body.contains("number") || !body["number"].is_number_integer()) {
            throw HttpError(400, "Missing whole-number field: number");
        }
        long long number = body["number"].get<long long>();    // not an int: a huge number must not wrap around into a valid one
        if (number < 1 || number > 25) {
            throw HttpError(400, "The number must be from 1 to the cards your team still has to find");
        }
        in.room.match().giveClue(in.you.name, text, (int)number);
        return json{{"ok", true}};
    }));

    server.Post("/api/guess", endpoint([](const httplib::Request& req) {
        Visitor in = visitorOf(req);
        json body = bodyOf(req);
        if (!body.contains("index") || !body["index"].is_number_integer()) {
            throw HttpError(400, "Missing whole-number field: index");
        }
        long long index = body["index"].get<long long>();      // not an int: a huge number must not wrap around into a valid card
        if (index < 0 || index > 24) {
            throw HttpError(400, "The card must be between 0 and 24");
        }
        CardKey key = in.room.match().guess(in.you.name, (int)index);
        return json{{"key", toString(key)}};
    }));

    server.Post("/api/end-turn", endpoint([](const httplib::Request& req) {
        Visitor in = visitorOf(req);
        in.room.match().endTurn(in.you.name);
        return json{{"ok", true}};
    }));

    server.Post("/api/forfeit", endpoint([](const httplib::Request& req) {
        Visitor in = visitorOf(req);
        in.room.match().forfeit(in.you.name);
        return json{{"ok", true}};
    }));

    server.set_mount_point("/images", "assets/images");
    if (filesystem::is_directory("web")) {
        server.set_mount_point("/", "web");
    }

    cout << "Listening on http://" << host << ":" << port << endl;
    if (!server.listen(host, port)) {
        cerr << "Error: could not listen on " << host << ":" << port << endl;
        return 1;
    }
    return 0;
}
