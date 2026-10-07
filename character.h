#include <iostream>
#include <string>
using namespace std;

class Character {
    public:
        string name;
        string type;
        string universe;
        string image_path;

        Character() {
            name = "";
            type = "";
            universe = "";
            image_path = "";
        }
};