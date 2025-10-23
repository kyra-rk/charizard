# Carbon Estimator
An API service that accurately estimates a user's carbon footprint. Clients can interface with the service through several API methods to log trips and retrieve computed carbon footprint metrics and recommendations.

The server stores and aggregates logged data over configurable windows (week, month) to compute carbon footprint metrics and detect trends (for example, an increase in footprint if a user switches from subway to taxis). Clients can compare their footprint against global or peer averages while preserving anonymity.

See Github [Issues](https://github.com/kyra-rk/charizard/issues) page for ongoing project management!

## Key features
- Simple registration + API key model for clients
- Per-event storage of transportation activity and aggregated footprint metrics (weekly/monthly)
- Suggestions and analytics endpoints that provide tailored tips and peer comparisons
- Pluggable backing store (in-memory for testing, MongoDB for persistence)
- Admin endpoints for operators to inspect logs, clients, and clear data (protected by an operator API key)

## Clients
High level overview of possible clients likely to use our service: 

1) Personal Transportation Tracker App
- Use case: mobile or desktop app for individuals to submit activities, fetch a personal footprint, and receive reduction tips.
- Typical API calls: `POST /users/{id}/transit`, `GET /users/{id}/lifetime-footprint`, `GET /users/{id}/suggestions`

1) Corporate Sustainability Dashboard
- Use case: businesses upload employee transportation reports (flights, buses, company shuttles) and pull anonymized, aggregated analytics to track organizational trends and identify high-emission behaviors.
- Typical API calls: `POST /users/{id}/transit` for batch reports, `GET /users/{id}/analytics` for aggregated insights

1) School Sustainability Dashboard
- Use case: schools and universities push student travel data (school-sponsored trips) and pull anonymized reports to measure and report on campus sustainability efforts.
- Typical API calls mirror the Corporate dashboard flow: `POST /users/{id}/transit` and `GET /users/{id}/analytics`

## Client Endpoints

This section documents the operational entry points that clients (apps) and developers will use. It lists each route, required authentication, expected inputs and outputs, visible side-effects, and common status / error codes. Note the service enforces a simple registration + API key model: you must register as a client to receive a `user_id` and `api_key` before calling protected client endpoints.

For every client endpoint, the following needs to be taken cared of: 
- Authentication: client endpoints require the API key produced at registration to be sent in the `X-API-Key` HTTP header. Example: `X-API-Key: <api_key>`.
- Request/response format: all bodies for documented endpoints use JSON; responses are JSON and error responses follow the shape `{ "error": "<reason>" }`.
- Logging: every request is recorded (`timestam`, `method`, `path`, `status`, `duration`, `client_IP`, `user_id`) and persisted to the configured store (in-memory, for testing, or on MongoDB).

### Health Endpoints
  - Path: `GET /health`
  - Auth: none
  - Input: none
  - Output: 200 OK JSON: `{ "ok": true, "service": "charizard", "time": <unix_epoch> }`
  - Side-effects: writes a log record for the request
  - Errors: none (returns 200 when reachable)

### Register Endpoints (join as a client)
  - Path: `POST /users/register`
  - Auth: none
  - Input: JSON `{ "app_name": "your_app_name" }` (app_name is required string)
  - Output: 201 Created JSON `{ "user_id": "u_<...>", "api_key": "<raw_api_key>", "app_name": "your_app_name" }`
      - The server generates a `user_id` and a printable `api_key` and stores only a hashed form of the API key in the backing store. The raw `api_key` is shown only once in this response. If you lose it you must re-register to obtain a new pair.
  - Side-effects: persists `user_id` + hashed `api_key` + `app_name` to the store; writes a log record
  - Status codes / errors:
      - 201 Created on success
      - 400 Bad Request — invalid JSON or missing/invalid `app_name` (error codes: `invalid_json`, `missing_app_name`)

### Transit Event Endpoint
  - Path: `POST /users/:user_id/transit`
  - Auth: required — set header `X-API-Key: <api_key>` matching the `user_id`.
  - Input: JSON `{ "mode": "car|bus|bike|walk|...", "distance_km": <number>, "ts": <optional unix epoch> }`
      - `mode` must be a string. `distance_km` must be a number (kilometers). `ts` is optional; if omitted server will set the event timestamp to current time.
  - Output: 201 Created JSON `{ "status": "ok" }`
  - Side-effects: stores a `TransitEvent` in the backing store for the `user_id` and writes a log record
  - Status codes / errors:
      - 201 Created on success
      - 400 Bad Request — invalid JSON or missing fields (error codes: `invalid_json`, `missing_fields`)
      - 401 Unauthorized — missing or invalid `X-API-Key` for the `user_id` (error: `unauthorized`)
      - 404 Not Found — malformed path (error: `bad_path`)

### Lifetime Footprint Endpoint
  - Path: `GET /users/:user_id/lifetime-footprint`
  - Auth: required — `X-API-Key: <api_key>`
  - Input: none
  - Output: 200 OK JSON `{ "user_id": "u_...", "lifetime_kg_co2": <number>, "last_7d_kg_co2": <number>, "last_30d_kg_co2": <number> }`
      - Values are computed from recorded transit events by the store implementation.
  - Side-effects: none besides a log record
  - Status codes / errors:
      - 200 OK on success
      - 401 Unauthorized when API key is missing/invalid
      - 404 Not Found for malformed path

### Suggestions Endpoint
  - Path: `GET /users/:user_id/suggestions`
  - Auth: required — `X-API-Key: <api_key>`
  - Input: none
  - Output: 200 OK JSON `{ "user_id": "u_...", "suggestions": ["...", ...] }`
      - Suggestions are simple heuristics based on weekly CO2 (example: encourage public transit or biking)
  - Side-effects: none besides a log record
  - Status codes / errors: 200 OK, or 401 Unauthorized, or 404 Bad Path

### Analytics Endpoint
  - Path: `GET /users/:user_id/analytics`
  - Auth: required — `X-API-Key: <api_key>`
  - Input: none
  - Output: 200 OK JSON `{ "user_id": "u_...", "this_week_kg_co2": <number>, "peer_week_avg_kg_co2": <number>, "above_peer_avg": <bool> }`
      - `peer_week_avg_kg_co2` is computed across clients by the store implementation
  - Side-effects: none besides a log record
  - Status codes / errors: 200 OK, or 401 Unauthorized, or 404 Bad Path

### Common error responses to expect:
- Format: `{ "error": "<reason>" }` where `<reason>` is one of:
    - `invalid_json` — request body was not valid JSON
    - `missing_app_name` — register is missing required field
    - `missing_fields` — transit missing `mode` or `distance_km`
    - `unauthorized` — API key not present or does not match the `user_id`
    - `bad_path` — request path doesn't match expected pattern

### Examples

Register and then submit transit events:

```bash
# Register and capture the generated credentials
$ curl -s -X POST http://localhost:8080/users/register \
    -H 'Content-Type: application/json' \
    -d '{"app_name":"my-dev-app"}' | jq

# Use the returned user_id and api_key in subsequent calls
$ curl -X POST http://localhost:8080/users/<user_id>/transit \
    -H "Content-Type: application/json" \
    -H "X-API-Key: <api_key>" \
    -d '{"mode":"car","distance_km":5.2}'

$ curl -H "X-API-Key: <api_key>" http://localhost:8080/users/<user_id>/lifetime-footprint | jq
```

### Ordering & operational caveats:
- You must `register` first to obtain a `user_id` and `api_key`. Calling protected endpoints (transit, lifetime-footprint, suggestions, analytics) before registering will return 401.
- Registering again (calling `/users/register` multiple times) will create an additional `user_id` for the same `app_name` — the service treats each registration as a distinct client.
- The raw `api_key` is shown only once at registration. The service stores only a hashed form of the API key; if you lose the key you must register again to receive a new `user_id`/`api_key`. You will no longer have access to the data stored through your previous API key.
- For local development (and testing) you can use the in-memory store (no persistence). If you use MongoDB, events, API key hashes, and logs are persisted to the configured database.

## Admin endpoints / API key

The service also exposes several admin-only endpoints (for example: viewing and clearing request logs, inspecting client data, and clearing the database). These endpoints are protected with a single operator API key which the running process reads from the `ADMIN_API_KEY` environment variable at startup.

Before starting the server you should export a secret value for `ADMIN_API_KEY`. For development you can use a simple placeholder, but never use the production operator key in public or shared environments.

Example (temporary, development use):

Set the admin key for this shell session and start the server
```bash
$ export ADMIN_API_KEY=changeme_admin_key_please_replace
$ make run
```

Use the admin key as a Bearer token in the `Authorization` header when calling admin endpoints. For example, to list stored logs:

```bash
$ curl -H "Authorization: Bearer changeme_admin_key_please_replace" \
    http://localhost:8080/admin/logs | jq
```

### Notes:
- Creating a `.env` file in the repository will not automatically export variables into a running process. Either export the variable in your shell before starting the server, or start the process with the variable inline (for one-off runs):

```bash
$ ADMIN_API_KEY=changeme_admin_key_please_replace make run
```

- The admin key is checked at server startup via `getenv("ADMIN_API_KEY")`. If the environment variable is not present when the server starts, calls to admin endpoints will return 401 Unauthorized.

### Examples

Replace `changeme_admin_key_please_replace` with your exported admin key.

```bash
# List logs (GET /admin/logs)
$ curl -H "Authorization: Bearer changeme_admin_key_please_replace" http://localhost:8080/admin/logs | jq

# Delete logs (DELETE /admin/logs)
$ curl -X DELETE -H "Authorization: Bearer changeme_admin_key_please_replace" http://localhost:8080/admin/logs

# List registered clients (GET /admin/clients)
$ curl -H "Authorization: Bearer changeme_admin_key_please_replace" http://localhost:8080/admin/clients | jq

# Get a client's stored events (GET /admin/clients/<user_id>/data)
$ curl -H "Authorization: Bearer changeme_admin_key_please_replace" http://localhost:8080/admin/clients/<user_id>/data | jq

# Clear only events collection (GET /admin/clear-db-events)
$ curl -H "Authorization: Bearer changeme_admin_key_please_replace" http://localhost:8080/admin/clear-db-events

# Clear entire DB (events, api_keys, logs) (GET /admin/clear-db)
$ curl -H "Authorization: Bearer changeme_admin_key_please_replace" http://localhost:8080/admin/clear-db
```

## Development
Development Tools:
- [cpp-httplib](https://github.com/yhirose/cpp-httplib): An easy RESTful API library for C++ developers
- MongoDB: A document database for persistent storage of carbon footprint data
- CMake and Make: Build/dependency manager
- GoogleTest: Testing framework for unit and integration tests
- `.clang-format` and `.clang-tidy`: Lint and check style

## Building & Running the Service
The `Makefile` lets you use many shortcuts to conveniently build the service with different
configurations as needed. To see a full menu of available targets, run

    $ make help

To build the service, run

    $ make build

To run the executable, run

    $ make run

To run through tests, run

    $ make test # or make test-verbose

You can also rebuild the service by running

    $ make rebuild # this will be useful when you edit something

To run the style checker, run

    $ make format

To perform a static analysis on your code, run

    $ make lint

If your lint errors are easy enough to fix, you can run

    $ make lint-fix

## Fast-Path
Running the program is simple:
```
  $ brew bundle
  $ make build && make run
```

For convenience during development you can use the provided helper script which exports a temporary `ADMIN_API_KEY` and runs the server:
```
  $ ./scripts/dev-start.sh
```

From there, you can send the service API requests via `curl` or any tool of your choice. For example,
```
  $ curl -s http://localhost:8080/health | jq
  {
      "ok": true,
      "service": "charizard",
      "time": 1761091516
  }
```