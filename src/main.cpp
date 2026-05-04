#include "Engine/Engine.hpp"

int main() {
    if (Engine::Create() != IncResult::SUCCESS) {
        analog::info("Couldn't create engine");
        return -1;
    }

    Engine::Run();

    Engine::Destroy();

    return 0;
}
