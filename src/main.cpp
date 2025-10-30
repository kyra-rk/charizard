#include <cstdlib>
#include <iostream>
#include <memory>
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include "api.hpp"
#include "storage.hpp"

#include <httplib.h>
#ifdef CHARIZARD_WITH_MONGO
#include "mongo_store.hpp"
#endif

// NOLINTNEXTLINE(misc-use-anonymous-namespace)
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
    try
    {
        auto store = make_store();
        store->set_api_key("demo", "secret-demo-key");

        httplib::Server svr;
        configure_routes(svr, *store);

        const char*       env_port  = std::getenv("PORT");
        int const         port      = (env_port != nullptr) ? std::atoi(env_port) : 8080;
        const char*       host      = std::getenv("HOST");
        std::string const bind_host = (host != nullptr) ? host : "0.0.0.0";

        std::cout << "[charizard] listening on " << bind_host << ":" << port << '\n';
        svr.listen(bind_host, port);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr << "Fatal error: unknown exception\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}