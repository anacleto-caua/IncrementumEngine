#include "Engine/Engine.hpp"

int main() {
    analog::init();
    if (Engine::Create() != IncResult::SUCCESS) {
        analog::error("Couldn't create engine");
        return -1;
    }

    Engine::Run();

    Engine::Destroy();

    return 0;
}
