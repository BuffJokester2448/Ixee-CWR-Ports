#include <Poseidon/Foundation/PoseidonPCH.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include <Poseidon/Core/IdString.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkMessages.hpp>

// Robustness regressions for the network message decoder. Each case drives the
// real DecodeMessage path with crafted wire bytes and asserts the decoder
// rejects malformed input cleanly instead of reading or allocating past the
// buffer. They lock the input validation in place so a later refactor can be
// proven not to reopen it.

namespace
{
// NCTSmallUnsigned varint, byte-compatible with NetworkMessageRaw::Put(int, ...).
void PutVarint(std::vector<uint8_t>& buf, uint32_t v)
{
    while (true)
    {
        uint8_t c = static_cast<uint8_t>(v & 0x7f);
        v >>= 7;
        if (v != 0)
        {
            buf.push_back(static_cast<uint8_t>(c | 0x80));
        }
        else
        {
            buf.push_back(c);
            break;
        }
    }
}

void PutBytes(std::vector<uint8_t>& buf, int n)
{
    for (int i = 0; i < n; i++)
    {
        buf.push_back(0);
    }
}

// Minimal concrete NetworkComponent that drives the real DecodeMessage with no
// dispatch, so a test can feed it raw bytes directly.
class DecodeHarness : public NetworkComponent
{
  public:
    DecodeHarness() : NetworkComponent(nullptr) {}

    NetworkMessageFormatBase* GetFormat(int type) override
    {
        if (type < 0 || type >= NMTN)
        {
            return nullptr;
        }
        return GMsgFormats[type];
    }
    void OnMessage(int from, NetworkMessage* msg, NetworkMessageType type) override {}
    void OnSimulate() override {}
    unsigned CleanUpMemory() override { return 0; }
    const char* GetDebugName() const override { return "decode-harness"; }
    bool DXSendMsg(int to, NetworkMessageRaw& rawMsg, DWORD& msgID, NetMsgFlags dwFlags) override { return false; }
    void EnqueueMsg(int to, NetworkMessage* msg, NetworkMessageType type) override {}
    void EnqueueMsgNonGuaranteed(int to, NetworkMessage* msg, NetworkMessageType type) override {}
    NetworkObject* GetObject(NetworkId& id) override { return nullptr; }

    void Decode(std::vector<uint8_t>& bytes)
    {
        NetworkMessageRaw raw(reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()));
        DecodeMessage(2, raw);
    }
};

// One harness for the whole binary; InitMsgFormats() populates GMsgFormats once.
DecodeHarness& Harness()
{
    static const bool inited = []
    {
        InitMsgFormats();
        return true;
    }();
    (void)inited;
    static DecodeHarness h;
    return h;
}

// Exposes the protected Read so a test can drive its bounds check directly.
struct ProbeRaw : public NetworkMessageRaw
{
    ProbeRaw(char* buf, int size) : NetworkMessageRaw(buf, size) {}
    bool ProbeRead(void* dst, int size) { return Read(dst, size); }
};
} // namespace

TEST_CASE("decoder rejects a negative wire message type", "[network][decode]")
{
    // type decodes to -1 (0xFFFFFFFF varint), followed by a 4-byte time header.
    // GetFormat must return nullptr rather than index the format table with it.
    std::vector<uint8_t> msg;
    PutVarint(msg, 0xFFFFFFFFu); // (int)type == -1
    PutBytes(msg, 4);            // time
    Harness().Decode(msg);
    SUCCEED("negative wire type handled without an out-of-range table index");
}

TEST_CASE("decoder accepts a well-formed empty aggregate", "[network][decode]")
{
    // NMTMessages with a sub-message count of zero — the valid path through GetCount.
    std::vector<uint8_t> msg;
    PutVarint(msg, static_cast<uint32_t>(NMTMessages));
    PutBytes(msg, 4);  // time
    PutVarint(msg, 0); // sub-message count = 0
    Harness().Decode(msg);
    SUCCEED("empty aggregate decoded");
}

TEST_CASE("decoder stops on a truncated header", "[network][decode]")
{
    std::vector<uint8_t> empty;
    Harness().Decode(empty); // no type byte -> header Get fails -> early return
    SUCCEED("empty buffer handled");
}

TEST_CASE("NetworkMessageRaw::GetCount bounds an element count to the input", "[network][decode]")
{
    SECTION("negative count rejected")
    {
        std::vector<uint8_t> buf;
        PutVarint(buf, 0xFFFFFFFFu); // decodes to -1
        PutBytes(buf, 8);
        NetworkMessageRaw raw(reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()));
        int count = 1234;
        REQUIRE_FALSE(raw.GetCount(count, NCTSmallUnsigned));
    }
    SECTION("count larger than the remaining bytes rejected")
    {
        std::vector<uint8_t> buf;
        PutVarint(buf, 1000); // claims 1000 elements...
        PutBytes(buf, 4);     // ...but only 4 bytes remain
        NetworkMessageRaw raw(reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()));
        int count = 0;
        REQUIRE_FALSE(raw.GetCount(count, NCTSmallUnsigned));
    }
    SECTION("an in-range count is accepted")
    {
        std::vector<uint8_t> buf;
        PutVarint(buf, 3);
        PutBytes(buf, 8);
        NetworkMessageRaw raw(reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()));
        int count = -1;
        REQUIRE(raw.GetCount(count, NCTSmallUnsigned));
        REQUIRE(count == 3);
    }
}

TEST_CASE("NetworkMessageRaw::Read bounds check is overflow-safe", "[network][decode]")
{
    char buf[4] = {1, 2, 3, 4};
    char dst[8] = {};

    SECTION("a size that overflows the position sum is rejected")
    {
        ProbeRaw raw(buf, sizeof(buf));
        char one;
        REQUIRE(raw.ProbeRead(&one, 1)); // advance _pos to 1
        REQUIRE_FALSE(raw.ProbeRead(dst, 0x7FFFFFFF));
    }
    SECTION("a negative size is rejected")
    {
        ProbeRaw raw(buf, sizeof(buf));
        REQUIRE_FALSE(raw.ProbeRead(dst, -1));
    }
    SECTION("a read past the end is rejected, a read to the end succeeds")
    {
        ProbeRaw raw(buf, sizeof(buf));
        REQUIRE_FALSE(raw.ProbeRead(dst, 5)); // one past the end
        REQUIRE(raw.ProbeRead(dst, 4));       // exactly to the end
    }
}

TEST_CASE("IdStringTable::GetString bounds the id before indexing", "[network][decode]")
{
    RStringB names[3] = {RStringB("alpha"), RStringB("bravo"), RStringB("charlie")};
    Poseidon::IdStringTable table(names, 3);

    // A valid id resolves to a real entry; out-of-range ids return an empty
    // string instead of indexing the name array past its bounds.
    REQUIRE(table.GetString(0).GetLength() > 0);
    REQUIRE(table.GetString(-1).GetLength() == 0);
    REQUIRE(table.GetString(3).GetLength() == 0);
    REQUIRE(table.GetString(0x7FFFFFFF).GetLength() == 0);
}
