#include <halley.hpp>
using namespace Halley;

IHalleyEntryPoint* getHalleyEntryStatic();


HALLEY_EXPORT IHalleyEntryPoint* getHalleyEntry()
{
    return getHalleyEntryStatic();
}

#ifndef WITH_STEAM
// Dummy implementation
void initSteamPlugin(Halley::IPluginRegistry& registry, int steamId, Halley::String initialLobby) {}
#endif
