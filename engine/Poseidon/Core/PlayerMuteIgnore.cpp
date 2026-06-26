#include <Poseidon/Core/PlayerMuteIgnore.hpp>

#include <mutex>
#include <unordered_set>

namespace Poseidon
{
namespace
{
static std::mutex& GetLock() {
    static std::mutex* m = new std::mutex();
    return *m;
}
std::unordered_set<int> g_mutedVoice;
std::unordered_set<int> g_ignoredChat;
bool g_muteAll = false;
bool g_ignoreAll = false;
} // namespace

bool IsVoiceMuted(int playerId)
{
    std::lock_guard<std::mutex> lk(GetLock());
    return g_muteAll || g_mutedVoice.find(playerId) != g_mutedVoice.end();
}

bool IsChatIgnored(int playerId)
{
    std::lock_guard<std::mutex> lk(GetLock());
    return g_ignoreAll || g_ignoredChat.find(playerId) != g_ignoredChat.end();
}

void SetVoiceMuted(int playerId, bool muted)
{
    std::lock_guard<std::mutex> lk(GetLock());
    if (muted)
        g_mutedVoice.insert(playerId);
    else
        g_mutedVoice.erase(playerId);
}

void SetChatIgnored(int playerId, bool ignored)
{
    std::lock_guard<std::mutex> lk(GetLock());
    if (ignored)
        g_ignoredChat.insert(playerId);
    else
        g_ignoredChat.erase(playerId);
}

bool ToggleVoiceMute(int playerId)
{
    std::lock_guard<std::mutex> lk(GetLock());
    if (g_mutedVoice.erase(playerId) != 0)
        return false;
    g_mutedVoice.insert(playerId);
    return true;
}

bool ToggleChatIgnore(int playerId)
{
    std::lock_guard<std::mutex> lk(GetLock());
    if (g_ignoredChat.erase(playerId) != 0)
        return false;
    g_ignoredChat.insert(playerId);
    return true;
}

void SetVoiceMutedAll(bool muted)
{
    std::lock_guard<std::mutex> lk(GetLock());
    g_muteAll = muted;
}

void SetChatIgnoredAll(bool ignored)
{
    std::lock_guard<std::mutex> lk(GetLock());
    g_ignoreAll = ignored;
}

void ClearMuteIgnore()
{
    std::lock_guard<std::mutex> lk(GetLock());
    g_muteAll = false;
    g_ignoreAll = false;
    g_mutedVoice.clear();
    g_ignoredChat.clear();
}
} // namespace Poseidon
