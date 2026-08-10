#include "Game/Game.hpp"

int main() {
    analog::init();

    if (Game::Create() != IncResult::SUCCESS) {
        analog::error("couldn't create game");
        return -1;
    };

    Game::Run();

    Game::Destroy();

    return 0;
}
