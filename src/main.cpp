#include <cstdlib>
#include <iostream>
#include <memory>
#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include "api.hpp"
#include "storage.hpp"

#include <httplib.h>
#ifdef CHARIZARD_WITH_MONGO
#include "mongo_store.hpp"
#endif

static std::unique_ptr<IStore> make_store()
{
#ifdef CHARIZARD_WITH_MONGO
    if (const char* uri = std::getenv("MONGO_URI"))
        return std::make_unique<MongoStore>(std::string{ uri });
#endif
    return std::make_unique<InMemoryStore>();
}

int main()
{
    auto store = make_store();
    store->set_api_key("demo", "secret-demo-key");

    httplib::Server svr;
    configure_routes(svr, *store);

    const char* env_port  = std::getenv("PORT");
    int         port      = env_port ? std::atoi(env_port) : 8080;
    const char* host      = std::getenv("HOST");
    std::string bind_host = host ? host : "0.0.0.0";

    std::cout << "[charizard] listening on " << bind_host << ":" << port << std::endl;
    svr.listen(bind_host.c_str(), port);
}