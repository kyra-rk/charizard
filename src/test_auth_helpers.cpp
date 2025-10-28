#include "test_auth_helpers.hpp"

bool test_check_auth(IStore& store, const httplib::Request& req, const std::string& user_id)
{
    auto it = req.headers.find("X-API-Key");
    if (it == req.headers.end())
        return false;
    return store.check_api_key(user_id, it->second);
}
