#include "storage.hpp"
#include "test_auth_helpers.hpp"

#include <gtest/gtest.h>

TEST(AuthStore, SetAndCheckApiKey)
{
    InMemoryStore s;
    s.set_api_key("alice", "key123", "myapp");
    EXPECT_TRUE(s.check_api_key("alice", "key123"));
    EXPECT_FALSE(s.check_api_key("alice", "wrong"));
    EXPECT_FALSE(s.check_api_key("bob", "key123"));
}

TEST(AuthStore, OverwriteApiKey)
{
    InMemoryStore s;
    s.set_api_key("u1", "first", "app1");
    EXPECT_TRUE(s.check_api_key("u1", "first"));
    s.set_api_key("u1", "second", "app2");
    EXPECT_FALSE(s.check_api_key("u1", "first"));
    EXPECT_TRUE(s.check_api_key("u1", "second"));
}

TEST(AuthStore, MultipleUsers)
{
    InMemoryStore s;
    s.set_api_key("a", "ka");
    s.set_api_key("b", "kb");
    EXPECT_TRUE(s.check_api_key("a", "ka"));
    EXPECT_TRUE(s.check_api_key("b", "kb"));
    EXPECT_FALSE(s.check_api_key("a", "kb"));
}

TEST(AuthHeaders, MissingHeaderFails)
{
    InMemoryStore s;
    s.set_api_key("demo", "secret-demo-key");
    httplib::Request const req;
    // no headers set
    EXPECT_FALSE(test_check_auth(s, req, "demo"));
}

TEST(AuthHeaders, WrongHeaderNameFails)
{
    InMemoryStore s;
    s.set_api_key("demo", "secret-demo-key");
    httplib::Request req;
    req.headers.insert({ "Authorization", "secret-demo-key" }); // wrong header name
    EXPECT_FALSE(test_check_auth(s, req, "demo"));
}

TEST(AuthHeaders, WrongKeyFails)
{
    InMemoryStore s;
    s.set_api_key("demo", "secret-demo-key");
    httplib::Request req;
    req.headers.insert({ "X-API-Key", "not-the-key" });
    EXPECT_FALSE(test_check_auth(s, req, "demo"));
}

TEST(AuthHeaders, CorrectKeySucceeds)
{
    InMemoryStore s;
    s.set_api_key("demo", "secret-demo-key");
    httplib::Request req;
    req.headers.insert({ "X-API-Key", "secret-demo-key" });
    EXPECT_TRUE(test_check_auth(s, req, "demo"));
}
