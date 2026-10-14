// HTTP server for the board game: one Lobby, then one Match, shared by every browser that connects.
//
// Build: make server
// Run:   build/server [port] [host]      (defaults: 8080 127.0.0.1) - run it from the project folder.
//
// Every endpoint answers JSON. A rejected action answers {"error": "..."} with a 4xx status.
// Players identify themselves with the token they get from /api/join, sent in the X-Token header.
//
// Waiting room (until every seat is taken and everybody is ready, then the match starts by itself):
//   GET  /api/lobby      who is in the waiting room (team and role are null until picked, plus "ready") and the open seats
//   POST /api/join       {"name"} -> {"token", ...}
//   POST /api/seat       {"team", "role"} picks or changes your seat; not while ready      (X-Token)
//   POST /api/ready      {"ready": true|false}                                             (X-Token)
//   POST /api/leave      leaves the waiting room                                           (X-Token)
// Match:
//   GET  /api/state      the turn, the cards left, the winner, and who you are             (X-Token)
//   GET  /api/board      the 25 cards as YOU may see them                                  (X-Token)
//   POST /api/guess      {"index": 0-24} -> {"key"}                                        (X-Token)
//   POST /api/end-turn                                                                     (X-Token)
// /images/... serves assets/images/ (the card pictures) and, if a web/ folder exists, / serves the front-end.
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <functional>
#include <filesystem>
#include "httplib.h"
#include "json.hpp"
#include "db/database.h"
#include "game/lobby.h"
#include "game/match.h"

using namespace std;
using json = nlohmann::json;

// An error with the HTTP status it should be answered with.
struct HttpError : runtime_error {
    int status;
    HttpError(int status, const string& message) : runtime_error(message), status(status) {}
};

// The whole game lives here. Requests run on several threads, so every handler holds the lock.
struct GameServer {
    mutex lock;
    vector<Character> characters;
    Lobby lobby;
    unique_ptr<Match> match;            // created when every seat is taken and everybody is ready
    map<string, string> nameOfToken;
};

GameServer game;

string newToken() {
    random_device device;
    char token[33];
    snprintf(token, sizeof(token), "%08x%08x%08x%08x", device(), device(), device(), device());
    return token;
}

// The player who sent the request, found through the X-Token header.
const Member& memberOf(const httplib::Request& req) {
    auto token = game.nameOfToken.find(req.get_header_value("X-Token"));
    if (token != game.nameOfToken.end()) {
        for (const Member& member : game.lobby.members()) {
            if (member.name == token->second) return member;
        }
    }
    throw HttpError(401, "Unknown or missing token. Join the game first");
}

// The waiting room is closed once the match has started.
void requireWaitingRoom() {
    if (game.match) {
        throw HttpError(409, "The game has already started");
    }
}

Match& startedMatch() {
    if (!game.match) {
        throw HttpError(409, "The game has not started yet");
    }
    return *game.match;
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

// team and role are null while the player has not picked a seat.
json memberJson(const Member& member) {
    json answer = {{"name", member.name}, {"team", nullptr}, {"role", nullptr}, {"ready", member.ready}};
    if (member.seat) {
        answer["team"] = toString(member.seat->first);
        answer["role"] = toString(member.seat->second);
    }
    return answer;
}

bool booleanField(const json& body, const string& field) {
    if (!body.contains(field) || !body[field].is_boolean()) {
        throw HttpError(400, "Missing true/false field: " + field);
    }
    return body[field];
}

// Wraps an action: takes the lock, answers its JSON, and turns exceptions into error answers.
httplib::Server::Handler endpoint(function<json(const httplib::Request&)> action) {
    return [action](const httplib::Request& req, httplib::Response& res) {
        lock_guard<mutex> guard(game.lock);
        int status = 200;
        json answer;
        try {
            answer = action(req);
        } catch (const HttpError& e) {
            status = e.status;
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
        game.characters = loadCharacters(defaultDatabasePath);
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    // Checked now because the Match is created when the last player is ready, and it needs 25 characters to fill the board.
    if (game.characters.size() < 25) {
        cerr << "Error: the database needs at least 25 characters, found " << game.characters.size() << endl;
        return 1;
    }

    httplib::Server server;

    // Each browser keeps several connections open, and every open connection holds a worker thread.
    // With the default (small) pool, the images and the polls of a few players wait seconds in line.
    server.new_task_queue = [] { return new httplib::ThreadPool(64); };
    server.set_keep_alive_timeout(2);

    server.Get("/api/lobby", endpoint([](const httplib::Request&) {
        json players = json::array(), slots = json::array();
        for (const Member& member : game.lobby.members()) {
            players.push_back(memberJson(member));
        }
        for (const Slot& slot : game.lobby.openSlots()) {
            slots.push_back({{"team", toString(slot.first)}, {"role", toString(slot.second)}});
        }
        return json{{"started", game.match != nullptr}, {"totalSeats", game.lobby.totalSeats()},
                    {"players", players}, {"openSlots", slots}};
    }));

    server.Post("/api/join", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        requireWaitingRoom();

        game.lobby.join(textField(body, "name"));       // throws if not allowed

        // The lobby may have trimmed the name, so the token must point to the name it stored.
        string token = newToken();
        game.nameOfToken[token] = game.lobby.members().back().name;

        json answer = memberJson(game.lobby.members().back());
        answer["token"] = token;
        return answer;
    }));

    server.Post("/api/leave", endpoint([](const httplib::Request& req) {
        requireWaitingRoom();
        string name = memberOf(req).name;
        game.lobby.leave(name);
        for (auto token = game.nameOfToken.begin(); token != game.nameOfToken.end();) {
            token = token->second == name ? game.nameOfToken.erase(token) : next(token);
        }
        return json{{"ok", true}};
    }));

    server.Post("/api/seat", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        requireWaitingRoom();
        string name = memberOf(req).name;
        game.lobby.chooseSeat(name, teamFromString(textField(body, "team")), roleFromString(textField(body, "role")));
        return memberJson(memberOf(req));
    }));

    server.Post("/api/ready", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        requireWaitingRoom();
        game.lobby.setReady(memberOf(req).name, booleanField(body, "ready"));
        if (game.lobby.canStart()) {
            game.match = make_unique<Match>(game.lobby, game.characters);
        }
        json answer = memberJson(memberOf(req));
        answer["started"] = game.match != nullptr;
        return answer;
    }));

    server.Get("/api/state", endpoint([](const httplib::Request& req) {
        const Member& you = memberOf(req);
        json answer = {{"started", game.match != nullptr}, {"you", memberJson(you)}};
        if (game.match) {
            Match& match = *game.match;
            answer["finished"] = match.isFinished();
            answer["currentTeam"] = toString(match.currentTeam());
            answer["remaining"] = {{"BLUE", match.remaining(Team::BLUE)}, {"RED", match.remaining(Team::RED)}};
            answer["yourTurn"] = !match.isFinished() && you.seat->second == Role::GUESSER && you.seat->first == match.currentTeam();
            if (match.isFinished()) answer["winner"] = toString(match.winner());
        }
        return answer;
    }));

    server.Get("/api/board", endpoint([](const httplib::Request& req) {
        const Member& you = memberOf(req);
        json cards = json::array();
        int index = 0;
        for (const CardView& card : startedMatch().viewFor(you.name)) {
            cards.push_back({{"index", index++}, {"name", card.characterName}, {"image", card.image},
                             {"key", toString(card.key)}, {"state", toString(card.state)}});
        }
        return json{{"cards", cards}};
    }));

    server.Post("/api/guess", endpoint([](const httplib::Request& req) {
        const Member& you = memberOf(req);
        json body = bodyOf(req);
        if (!body.contains("index") || !body["index"].is_number_integer()) {
            throw HttpError(400, "Missing whole-number field: index");
        }
        long long index = body["index"].get<long long>();      // not an int: a huge number must not wrap around into a valid card
        if (index < 0 || index > 24) {
            throw HttpError(400, "The card must be between 0 and 24");
        }
        CardKey key = startedMatch().guess(you.name, (int)index);
        return json{{"key", toString(key)}};
    }));

    server.Post("/api/end-turn", endpoint([](const httplib::Request& req) {
        startedMatch().endTurn(memberOf(req).name);
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
