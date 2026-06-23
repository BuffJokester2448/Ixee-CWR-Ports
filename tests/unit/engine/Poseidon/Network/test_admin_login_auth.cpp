#include <catch2/catch_test_macros.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PreprocC/PreprocC.hpp>
#include <Poseidon/Network/NetworkServerAuth.hpp>
#include <cstring>
#include <catch2/catch_message.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

// Regression tests for dedicated-server admin login authorization. Admin is
// granted only when the server config defines a non-empty passwordAdmin and the
// supplied password matches it exactly; a missing or empty entry grants nothing.
// These drive the real ParamFile parser (the same class that parses server.cfg)
// against the engine predicate Poseidon::AdminLoginPasswordAccepted.

namespace
{
// Text-config parsing needs a preprocessor registered on ParamFile.
void EnsurePreproc()
{
    static CPreprocessorFunctions* fns = nullptr;
    if (!fns)
    {
        fns = new CPreprocessorFunctions();
        ParamFile::SetDefaultPreprocFunctions(fns);
    }
}

ParamFile ParseServerCfg(const char* cfg)
{
    EnsurePreproc();
    ParamFile pf;
    QIStream in(cfg, strlen(cfg));
    pf.Parse(in);
    return pf;
}

// The authorization spec the login handler enforces: admin is granted only when
// a non-empty passwordAdmin is configured and matched exactly.
bool AdminLoginGranted(ParamFile& serverCfg, const char* providedPassword)
{
    ParamEntry* entry = serverCfg.FindEntry("passwordAdmin");
    if (!entry)
    {
        return false; // no admin password configured -> no remote admin
    }
    RStringB realPassword = *entry;
    if (realPassword.GetLength() == 0)
    {
        return false; // empty configured password -> no remote admin
    }
    return strcmp(providedPassword, realPassword.Data()) == 0;
}
} // namespace

TEST_CASE("a server with no passwordAdmin grants admin to no one", "[network][admin]")
{
    // A realistic minimal dedicated-server config that simply omits passwordAdmin.
    const char* cfgNoAdminPw = "hostname = \"My OFP Server\";\n"
                               "maxPlayers = 32;\n"
                               "voteThreshold = 0.5;\n";

    ParamFile cfg = ParseServerCfg(cfgNoAdminPw);
    REQUIRE(cfg.FindEntry("passwordAdmin") == nullptr); // the precondition

    REQUIRE_FALSE(Poseidon::AdminLoginPasswordAccepted(cfg, ""));
    REQUIRE_FALSE(Poseidon::AdminLoginPasswordAccepted(cfg, "wrong"));
    REQUIRE_FALSE(Poseidon::AdminLoginPasswordAccepted(cfg, "literally anything"));
}

TEST_CASE("an empty passwordAdmin string grants admin to no one", "[network][admin]")
{
    const char* cfgEmptyPw = "hostname = \"Server\";\n"
                             "passwordAdmin = \"\";\n";

    ParamFile cfg = ParseServerCfg(cfgEmptyPw);
    REQUIRE(cfg.FindEntry("passwordAdmin") != nullptr);

    REQUIRE_FALSE(Poseidon::AdminLoginPasswordAccepted(cfg, ""));
    REQUIRE_FALSE(Poseidon::AdminLoginPasswordAccepted(cfg, "x"));
}

TEST_CASE("a configured passwordAdmin gates admin correctly", "[network][admin]")
{
    const char* cfgWithPw = "hostname = \"Server\";\n"
                            "passwordAdmin = \"s3cret\";\n"
                            "maxPlayers = 32;\n";

    ParamFile cfg = ParseServerCfg(cfgWithPw);
    REQUIRE(cfg.FindEntry("passwordAdmin") != nullptr);

    REQUIRE_FALSE(AdminLoginGranted(cfg, ""));       // empty rejected
    REQUIRE_FALSE(AdminLoginGranted(cfg, "wrong"));  // wrong rejected
    REQUIRE_FALSE(AdminLoginGranted(cfg, "S3CRET")); // case-sensitive
    REQUIRE(AdminLoginGranted(cfg, "s3cret"));       // correct -> admin
}

// AdminLoginGranted() above is the authorization spec; this asserts the engine
// predicate matches it across a matrix of configs and passwords. If the engine
// logic ever drifts from the spec, this fails.
TEST_CASE("AdminLoginPasswordAccepted matches the authorization spec", "[network][admin]")
{
    struct Case
    {
        const char* cfg;
        const char* pw;
    };
    const Case cases[] = {
        {"hostname = \"s\";\n", ""},
        {"hostname = \"s\";\n", "x"},
        {"hostname = \"s\";\nmaxPlayers = 8;\n", "anything"},
        {"passwordAdmin = \"\";\n", ""},
        {"passwordAdmin = \"\";\n", "x"},
        {"passwordAdmin = \"s3cret\";\n", ""},
        {"passwordAdmin = \"s3cret\";\n", "wrong"},
        {"passwordAdmin = \"s3cret\";\n", "S3CRET"},
        {"passwordAdmin = \"s3cret\";\n", "s3cret"},
    };
    for (const auto& c : cases)
    {
        ParamFile cfg = ParseServerCfg(c.cfg);
        INFO("cfg=[" << c.cfg << "] pw=[" << c.pw << "]");
        REQUIRE(Poseidon::AdminLoginPasswordAccepted(cfg, c.pw) == AdminLoginGranted(cfg, c.pw));
    }
}
