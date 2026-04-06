#include "Engine/Engine.hpp"

int main() {

    INC_CHECK(Engine::Create(), -1 ,"Couldn't create engine. {}", "fail");

    Engine::Run();

    Engine::Destroy();

    return 0;
}
