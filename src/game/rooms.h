#pragma once
#include <string>
#include <map>
#include <memory>
#include <chrono>
#include <random>
#include <cctype>
#include "room.h"
using namespace std;

// There is no room with that code.
struct NoSuchRoom : runtime_error {
    using runtime_error::runtime_error;
};

// The server cannot hold more rooms right now.
struct RoomLimit : runtime_error {
    using runtime_error::runtime_error;
};

// Every room of the server. Rooms are made with a random code, found by that code or by the token of one of their
// players, and deleted as soon as they have no players. A room that nobody touched for a long time (the players
// closed their pages without leaving) is forgotten as well.
// Not thread safe: whoever shares a RoomRegistry between threads locks it.
class RoomRegistry {
    public:
        typedef Room::Clock Clock;

        struct Entry {
            Room* room;
            string token;       // the secret token of the player who just entered
        };

        static constexpr int codeLength = 5;

    private:
        const Catalog& catalog;
        size_t maxRooms;
        Clock::duration idleLimit;
        map<string, unique_ptr<Room>> rooms;          // by code
        map<string, string> roomOfToken;              // the token of every player -> the code of their room

        // Letters and digits that cannot be mistaken for each other (no 0/O, 1/I/L).
        static const char* alphabet() {
            return "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
        }

        string freshCode() const {
            random_device device;
            string alphabetChars = alphabet();
            uniform_int_distribution<size_t> pick(0, alphabetChars.size() - 1);
            while (true) {
                string code;
                for (int i = 0; i < codeLength; i++) code += alphabetChars[pick(device)];
                if (!rooms.count(code)) return code;
            }
        }

        void forget(const string& code) {
            for (auto entry = roomOfToken.begin(); entry != roomOfToken.end();) {
                entry = entry->second == code ? roomOfToken.erase(entry) : next(entry);
            }
            rooms.erase(code);
        }

    public:
        RoomRegistry(const Catalog& catalog, size_t maxRooms = 200, Clock::duration idleLimit = chrono::hours(2))
            : catalog(catalog), maxRooms(maxRooms), idleLimit(idleLimit) {}

        // What a player types: spaces and hyphens do not matter, and neither does the case. "k7q-d2" is "K7QD2".
        static string normalizeCode(const string& typed) {
            string code;
            for (unsigned char c : typed) {
                if (isalnum(c)) code += (char)toupper(c);
            }
            return code;
        }

        // Makes a room with a new random code and puts the player in it. If the player cannot enter (for example, a
        // blank name) no room is left behind.
        Entry create(const string& playerName, Clock::time_point now = Clock::now()) {
            sweep(now);
            if (rooms.size() >= maxRooms) {
                throw RoomLimit("There are too many rooms right now. Try again in a few minutes");
            }
            string code = freshCode();
            unique_ptr<Room> room = make_unique<Room>(code, catalog, now);
            string token = room->join(playerName);
            Room* raw = room.get();
            rooms[code] = std::move(room);
            roomOfToken[token] = code;
            return Entry{raw, token};
        }

        // Puts the player in the room with that code.
        Entry join(const string& typedCode, const string& playerName, Clock::time_point now = Clock::now()) {
            sweep(now);
            auto found = rooms.find(normalizeCode(typedCode));
            if (found == rooms.end()) {
                throw NoSuchRoom("There is no room with that code");
            }
            Room* room = found->second.get();
            string token = room->join(playerName);
            room->touch(now);
            roomOfToken[token] = room->code();
            return Entry{room, token};
        }

        // The room of the player with that token. Throws NotInRoom if the token is unknown.
        Room& roomOf(const string& token, Clock::time_point now = Clock::now()) {
            auto code = roomOfToken.find(token);
            if (code != roomOfToken.end()) {
                Room& room = *rooms.at(code->second);
                room.touch(now);                  // before the sweep, so that a room in use is never forgotten
                sweep(now);
                return room;
            }
            sweep(now);
            throw NotInRoom("Unknown or missing token. Join the game first");
        }

        // The player leaves their room. A room that is left empty disappears.
        void leave(const string& token, Clock::time_point now = Clock::now()) {
            Room& room = roomOf(token, now);
            string code = room.code();
            room.leave(token);
            roomOfToken.erase(token);
            if (room.isEmpty()) {
                forget(code);
            }
        }

        // Forgets the rooms that nobody has touched for longer than the idle limit.
        void sweep(Clock::time_point now) {
            vector<string> idle;
            for (const auto& room : rooms) {
                if (now - room.second->lastUsed() > idleLimit) idle.push_back(room.first);
            }
            for (const string& code : idle) forget(code);
        }

        bool contains(const string& typedCode) const {
            return rooms.count(normalizeCode(typedCode)) > 0;
        }

        size_t size() const {
            return rooms.size();
        }
};
