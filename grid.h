#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <stdexcept>
#include "card.h"
using namespace std;

// Single random engine shared by the whole program, seeded once.
inline mt19937& rng() {
    static mt19937 gen(random_device{}());
    return gen;
}

string KPossibilities[25] = {
    "BLUE", "BLUE", "BLUE", "BLUE", "BLUE", "BLUE", "BLUE", "BLUE", 
    "RED", "RED", "RED", "RED", "RED", "RED", "RED", "RED", 
    "BLACK", 
    "BEIGE", "BEIGE", "BEIGE", "BEIGE", "BEIGE", "BEIGE", "BEIGE", "BEIGE"
};

class Grid {
    public: 
        Card lineup[25];
        vector<Character> pool;

        // pool: every character the grid may draw from (e.g. the result of loadCharacters()).
        Grid(vector<Character> pool) {
            this->pool = pool;
        }

        // Initializes the 25 Cards in lineup with random, distinct characters from the pool, using init().
        // init() resets the key, so call this before assignKeyCards().
        void assignCharacters() {
            if (pool.size() < 25) {
                throw invalid_argument("Not enough characters to fill the grid: " + to_string(pool.size()) + " < 25");
            }

            vector<Character> shuffled = pool;
            shuffle(shuffled.begin(), shuffled.end(), rng());

            for (int i = 0; i < 25; i++) {
                lineup[i].init(shuffled[i]);
            }
        }

        // Sets the key of each Card in the lineup to a random pick from a copy of KPossibilities.
        // Every time a key is assigned, it is removed from the copy; the copy is discarded at the end.
        void assignKeyCards() {
            vector<string> remaining(KPossibilities, KPossibilities + 25);

            for (int i = 0; i < 25; i++) {
                uniform_int_distribution<int> pick(0, remaining.size() - 1);
                int index = pick(rng());

                lineup[i].key = remaining[index];
                remaining.erase(remaining.begin() + index);
            }
        }

};