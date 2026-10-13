#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include <sqlite3.h>
#include "game/character.h"
using namespace std;

// Where the characters live, relative to the project folder (programs are run from there).
inline const string defaultDatabasePath = "data/characters.db";

// Reads a TEXT column as a string, treating NULL as "".
inline string columnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? string(reinterpret_cast<const char*>(text)) : "";
}

// Loads every row of the "marvel" table as a Character.
// image_path is the picture folder as the front-end asks for it (images/...), served from assets/images/.
inline vector<Character> loadCharacters(const string& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        string message = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        throw runtime_error("Could not open database '" + dbPath + "': " + message);
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name, type, universe, image_path FROM marvel", -1, &stmt, nullptr) != SQLITE_OK) {
        string message = sqlite3_errmsg(db);
        sqlite3_close(db);
        throw runtime_error("Could not read characters: " + message);
    }

    vector<Character> characters;
    try {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            characters.push_back(Character(
                columnText(stmt, 0),
                characterTypeFromString(columnText(stmt, 1)),
                universeFromString(columnText(stmt, 2)),
                columnText(stmt, 3)
            ));
        }
    } catch (...) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        throw;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return characters;
}
