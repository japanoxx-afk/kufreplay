#pragma once

#ifdef _WIN32
struct ReplayStartConfig {
    wchar_t root[1024];
    wchar_t input[1024];
    wchar_t event[128];
    unsigned int playback;
};
#endif
#include <cstdint>
namespace kuf {
constexpr char Magic[8] = {'K','U','F','T','R','C','0','1'};
constexpr uint32_t MaxCommand = 2048;
constexpr uint32_t MaxFile = 128u * 1024 * 1024;
// TrimEnd is internal to the writer queue; archives contain only standard End records.
enum Kind : uint32_t { Begin = 1, Before = 2, Command = 3, After = 4, End = 5, TrimEnd = 6, Setup = 7, Bootstrap = 8, Names = 9, Population = 10 };
#pragma pack(push, 1)
// Population is writer-only metadata, never part of the replay command stream.
struct PopulationSample { uint32_t ordinal, tick, player, used, capacity; };
struct Record { uint32_t kind, size; };
struct State { uint32_t tick, seed, randomCount, checksum; };
struct Initial {
    State state;
    uint32_t networkMode;
    char directory[256], map[256];
    uint32_t slots[11][3];
};
struct CommandHeader { uint32_t tick, seed, randomCount, size; };
struct PlayerNames { uint32_t version; char names[8][64]; };
struct MatchSetup {
    uint32_t version, scenarioMode, rtsMode, gameType, localPlayer;
    uint32_t players[8], resources[8];
    unsigned char nativeOptions[16];
};
// Values sampled before native map/unit initialization, never process pointers.
struct NetworkBootstrap {
    uint32_t version, scenarioMode, rtsMode, participants, localPlayer;
    uint32_t seed, randomCount, tickInterval;
    uint32_t slots[11][4]; // controller, race, descriptor field +8, readiness byte
    unsigned char nativeOptions[16];
};
#pragma pack(pop)
static_assert(sizeof(State) == 16);
static_assert(sizeof(Initial) == 664);
static_assert(sizeof(MatchSetup) == 100);
static_assert(sizeof(NetworkBootstrap) == 224);
}
