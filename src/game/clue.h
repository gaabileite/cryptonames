#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include "enums.h"
using namespace std;

// A clue a SPY gives to the GUESSER of the team: one or two words and the number of cards that fit it.
struct Clue {
    Team team;
    string text;       // 1 or 2 words, spaces around and between them already cleaned
    int number;        // how many cards fit the clue = how many tries the GUESSER has for it
    int bonusTries;    // extra tries that came from the previous turn (see Match)
    int found;         // how many of this clue's cards were found so far
};

constexpr int maxClueWords = 2;
constexpr size_t maxClueLength = 40;

// The clue text without the spaces around it and with a single space between the words.
// Throws invalid_argument (with a message for the player) if it is empty, has more than 2 words,
// is too long or has control characters.
inline string cleanClueText(const string& raw) {
    for (unsigned char c : raw) {
        bool whitespace = c == ' ' || (c >= '\t' && c <= '\r');       // tabs and line breaks count as spaces
        if (!whitespace && (c < 0x20 || c == 0x7f)) {
            throw invalid_argument("A clue cannot have control characters");
        }
    }
    istringstream words(raw);
    vector<string> parts;
    string word;
    while (words >> word) parts.push_back(word);

    if (parts.empty()) {
        throw invalid_argument("Type a clue");
    }
    if ((int)parts.size() > maxClueWords) {
        throw invalid_argument("A clue can have at most " + to_string(maxClueWords) + " words");
    }
    string text = parts[0];
    for (size_t i = 1; i < parts.size(); i++) text += " " + parts[i];
    if (text.size() > maxClueLength) {
        throw invalid_argument("A clue can have at most " + to_string(maxClueLength) + " characters");
    }
    return text;
}
