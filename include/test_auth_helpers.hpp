#pragma once

#include "storage.hpp"

#include <httplib.h>

// Lightweight helper for tests that checks API key header against store.
// Mirrors the real route's auth logic: looks for X-API-Key header and calls store.check_api_key.
bool test_check_auth(IStore& store, const httplib::Request& req, const std::string& user_id);
