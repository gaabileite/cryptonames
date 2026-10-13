// HTTP server for the board game: one Lobby, then one Match, shared by every browser that connects.
//
// Build: g++ -std=c++17 server.cpp -lsqlite3 -pthread -o server
// Run:   ./server [port] [host]      (defaults: 8080 127.0.0.1) - run it from the project folder.
//
// Every endpoint answers JSON. A rejected action answers {"error": "..."} with a 4xx status.
// Players identify themselves with the token they get from /api/join, sent in the X-Token header.
//   GET  /api/lobby      who joined and which seats are still open
//   POST /api/join       {"name", "team", "role"} -> {"token", "name", "team", "role"}
//   GET  /api/state      the turn, the cards left, the winner, and who you are          (X-Token)
//   GET  /api/board      the 25 cards as YOU may see them                               (X-Token)
//   POST /api/guess      {"index": 0-24} -> {"key"}                                     (X-Token)
//   POST /api/end-turn                                                                  (X-Token)
// /images/... serves the card pictures and, if a web/ folder exists, / serves the front-end.
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <functional>
#include <filesystem>
#include "third_party/httplib.h"
#include "third_party/json.hpp"
#include "database.h"
#include "lobby.h"
#include "match.h"

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
    unique_ptr<Match> match;            // created when the last seat is taken
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
const Player& playerOf(const httplib::Request& req) {
    auto token = game.nameOfToken.find(req.get_header_value("X-Token"));
    if (token == game.nameOfToken.end()) {
        throw HttpError(401, "Unknown or missing token. Join the game first");
    }
    for (const Player& player : game.lobby.players()) {
        if (player.name == token->second) return player;
    }
    throw HttpError(401, "Unknown or missing token. Join the game first");
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

json playerJson(const Player& player) {
    return {{"name", player.name}, {"team", toString(player.team)}, {"role", toString(player.role)}};
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
        game.characters = loadCharacters("characters.db");
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    // Checked now because the Match is created when the last player joins, and it needs 25 characters to fill the board.
    if (game.characters.size() < 25) {
        cerr << "Error: the database needs at least 25 characters, found " << game.characters.size() << endl;
        return 1;
    }

    httplib::Server server;

    server.Get("/api/lobby", endpoint([](const httplib::Request&) {
        json players = json::array(), slots = json::array();
        for (const Player& player : game.lobby.players()) {
            players.push_back(playerJson(player));
        }
        for (const Slot& slot : game.lobby.openSlots()) {
            slots.push_back({{"team", toString(slot.first)}, {"role", toString(slot.second)}});
        }
        return json{{"started", game.match != nullptr}, {"players", players}, {"openSlots", slots}};
    }));

    server.Post("/api/join", endpoint([](const httplib::Request& req) {
        json body = bodyOf(req);
        if (game.match) {
            throw HttpError(409, "The game has already started");
        }

        game.lobby.addPlayer(textField(body, "name"), textField(body, "team"), textField(body, "role"));   // throws if not allowed

        // The lobby may have trimmed the name, so the token must point to the name it stored.
        string token = newToken();
        game.nameOfToken[token] = game.lobby.players().back().name;
        if (game.lobby.isFull()) {
            game.match = make_unique<Match>(game.lobby, game.characters);
        }

        json answer = playerJson(game.lobby.players().back());
        answer["token"] = token;
        return answer;
    }));

    server.Get("/api/state", endpoint([](const httplib::Request& req) {
        const Player& you = playerOf(req);
        json answer = {{"started", game.match != nullptr}, {"you", playerJson(you)}};
        if (game.match) {
            Match& match = *game.match;
            answer["finished"] = match.isFinished();
            answer["currentTeam"] = toString(match.currentTeam());
            answer["remaining"] = {{"BLUE", match.remaining(Team::BLUE)}, {"RED", match.remaining(Team::RED)}};
            answer["yourTurn"] = !match.isFinished() && you.role == Role::GUESSER && you.team == match.currentTeam();
            if (match.isFinished()) answer["winner"] = toString(match.winner());
        }
        return answer;
    }));

    server.Get("/api/board", endpoint([](const httplib::Request& req) {
        const Player& you = playerOf(req);
        json cards = json::array();
        int index = 0;
        for (const CardView& card : startedMatch().viewFor(you.name)) {
            cards.push_back({{"index", index++}, {"name", card.characterName}, {"image", card.image},
                             {"key", toString(card.key)}, {"state", toString(card.state)}});
        }
        return json{{"cards", cards}};
    }));

    server.Post("/api/guess", endpoint([](const httplib::Request& req) {
        const Player& you = playerOf(req);
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
        startedMatch().endTurn(playerOf(req).name);
        return json{{"ok", true}};
    }));

    server.set_mount_point("/images", "images");
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
