#pragma once
#include "storage.hpp"

#include <httplib.h>

// Adds all endpoints to `svr` using the given store.
void configure_routes(httplib::Server& svr, IStore& store);