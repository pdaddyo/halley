#include <halley.hpp>
using namespace Halley;

IHalleyEntryPoint* getHalleyEntryStatic();

#if !defined(_WIN32) && !defined(WITH_GDK)
#include <cstdlib>
static void applyPlatformHints() {
    // SDL's HIDAPI joystick init calls IOServiceOpen on macOS and can deadlock
    // in AppleSyntheticGameController when Input Monitoring permission is not
    // granted or a gamepad driver is in a bad state. Disable unless the user
    // has explicitly opted in via the environment variable.
    setenv("SDL_JOYSTICK_HIDAPI", "0", 0); // 0 = don't override if user set it
}
#else
static void applyPlatformHints() {}
#endif

#if defined(WITH_SDL3)

#include <SDL3/SDL_main.h>

extern "C" int main(int argc, char* argv[])
{
    applyPlatformHints();
    return HalleyMain::runMain(std::make_unique<EntryPointGameLoader>(*getHalleyEntryStatic()), HalleyMain::getArgs(argc, argv));
}

#elif defined(_WIN32) || defined(WITH_GDK)

int __stdcall WinMain(void*, void*, char*, int)
{
    applyPlatformHints();
    return HalleyMain::runMain(std::make_unique<EntryPointGameLoader>(*getHalleyEntryStatic()), HalleyMain::getWin32Args());
}

#else

int main(int argc, char* argv[])
{
    applyPlatformHints();
    return HalleyMain::runMain(std::make_unique<EntryPointGameLoader>(*getHalleyEntryStatic()), HalleyMain::getArgs(argc, argv));
}

#endif
