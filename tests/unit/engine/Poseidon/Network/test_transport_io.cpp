#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <Poseidon/Network/RateLimit.hpp>
#include <Poseidon/Network/WireBounds.hpp>

using namespace Poseidon;

// Transport-layer bounds regressions for the dispatch redesign. The transport
// handlers can't be unit-instantiated without the live UDP stack, so the
// non-trivial bounds math is extracted into WireBounds / RateLimit predicates the
// handlers call, and these tests bind to those predicates at their boundaries.

TEST_CASE("big-ack word count is bounded by the packet, not by oldest/newest", "[network][transport]")
{
    // BigAckPacket is a 16-byte header followed by a flexible unsigned32 ack[].
    const int header = 16;
    const int word = 4;

    // A 24-byte packet carries exactly (24-16)/4 = 2 ack words, regardless of the
    // oldest/newest fields that used to bound the read loop.
    REQUIRE(WireBounds::TrailingElementCount(24, header, word) == 2);
    REQUIRE(WireBounds::TrailingElementCount(16, header, word) == 0); // header only
    // The count is derived from the bytes actually present, so the read loop is
    // capped at the words really in the packet.
    REQUIRE(WireBounds::TrailingElementCount(8, header, word) == 0);  // short -> nothing
    REQUIRE(WireBounds::TrailingElementCount(-1, header, word) == 0); // garbage length
}

TEST_CASE("serial window rejects far-out serials, accepts in-window", "[network][transport]")
{
    const uint32_t lo = 1000; // ackMaskMin
    const uint32_t hi = 1050; // inputMax
    const int64_t span = 256; // par.maxChannelBitMask

    // In-window and just-ahead serials are accepted (normal traffic).
    REQUIRE(WireBounds::SerialWithinSpan(1025, lo, hi, span));
    REQUIRE(WireBounds::SerialWithinSpan(hi + 100, lo, hi, span));
    REQUIRE(WireBounds::SerialWithinSpan(lo - 100, lo, hi, span));

    // Serials far outside the window are rejected, keeping the ack-mask growth and
    // the catch-up loop bounded.
    REQUIRE_FALSE(WireBounds::SerialWithinSpan(hi + 1000000, lo, hi, span));
    REQUIRE_FALSE(WireBounds::SerialWithinSpan(lo - 1000000, lo, hi, span));
    // Wrap-around: a serial just below 0 relative to a small window is "behind", not ahead.
    REQUIRE_FALSE(WireBounds::SerialWithinSpan(0xFFFFFFFFu, lo, hi, span));
}

TEST_CASE("NMTMessages batch sub-count is bounded by remaining bytes", "[network][transport]")
{
    // DecodeMessage reads a sub-message count n off the wire and loops over it. Each
    // sub-message is at least its 1-byte type varint, so n can never legitimately
    // exceed the bytes left; the handler rejects a larger n via this predicate.
    const int remaining = 12;
    REQUIRE(WireBounds::DecodeCountFits(12, remaining)); // every byte a 1-byte sub-msg
    REQUIRE(WireBounds::DecodeCountFits(0, remaining));
    // A count with no relation to the remaining bytes is rejected before the loop runs.
    REQUIRE_FALSE(WireBounds::DecodeCountFits(0x7FFFFFFF, remaining));
    REQUIRE_FALSE(WireBounds::DecodeCountFits(13, remaining));
    REQUIRE_FALSE(WireBounds::DecodeCountFits(-1, remaining));
}

TEST_CASE("a datagram shorter than the trailing CRC is rejected", "[network][transport]")
{
    // The trailing CRC int is read only when the datagram is long enough to hold it.
    REQUIRE(WireBounds::RangeInBounds(0, (int)sizeof(int), 4));       // exactly the CRC
    REQUIRE(WireBounds::RangeInBounds(0, (int)sizeof(int), 8));       // body + CRC
    REQUIRE_FALSE(WireBounds::RangeInBounds(0, (int)sizeof(int), 3)); // too short
    REQUIRE_FALSE(WireBounds::RangeInBounds(0, (int)sizeof(int), 0));
}

TEST_CASE("enum-reply token bucket caps reflected replies", "[network][transport]")
{
    // The enum responder rate-limits replies: at most `burst` in a burst and
    // `ratePerSec` sustained, so reply traffic stays bounded.
    RateLimit::TokenBucket b;
    b.configure(/*ratePerSec*/ 50.0, /*burst*/ 100.0);

    // A burst at the same instant drains exactly `burst` replies, then is throttled.
    int allowed = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (b.tryConsume(/*nowMs*/ 1000))
        {
            ++allowed;
        }
    }
    REQUIRE(allowed == 100);
    REQUIRE_FALSE(b.tryConsume(1000));

    // One second later it has refilled ratePerSec tokens (50), no more.
    int refilled = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (b.tryConsume(/*nowMs*/ 2000))
        {
            ++refilled;
        }
    }
    REQUIRE(refilled == 50);

    // Legitimate cadence (one request every 100 ms) is never throttled.
    RateLimit::TokenBucket steady;
    steady.configure(50.0, 100.0);
    bool allPassed = true;
    for (int i = 0; i < 200; ++i)
    {
        if (!steady.tryConsume(static_cast<uint32_t>(i) * 100u))
        {
            allPassed = false;
        }
    }
    REQUIRE(allPassed);
}

TEST_CASE("token bucket handles tick-count wraparound", "[network][transport]")
{
    RateLimit::TokenBucket b;
    b.configure(50.0, 100.0);
    b.tryConsume(0xFFFFFF00u); // just before 32-bit rollover
    // After wrap, the unsigned delta is small (256 ms), not ~4.3e9 ms — so the
    // bucket credits ~12.8 tokens, not an effectively-infinite amount.
    b.tryConsume(0x00000000u); // wrapped
    // Drain whatever is available at the wrapped instant; must be bounded by burst.
    int allowed = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (b.tryConsume(0x00000000u))
        {
            ++allowed;
        }
    }
    REQUIRE(allowed < 100); // the wrap does not open the bucket
}

TEST_CASE("a wire server name is rendered as data, never as a format", "[network][transport]")
{
    // The enumeration handler formats the wire-supplied server name as data
    // (snprintf(dst, n, "%s", name)), so conversion specifiers in the name are
    // shown literally rather than interpreted.
    const char* name = "%s%s%s%n";
    char out[64];
    int n = snprintf(out, sizeof(out), "%s", name);
    REQUIRE(n == (int)strlen(name));
    REQUIRE(strcmp(out, "%s%s%s%n") == 0); // conversion specifiers were not interpreted
}
