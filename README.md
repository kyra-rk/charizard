# Carbon Estimator
An API service that accurately estimates a user's carbon footprint. Clients can interface with the service through several API methods to log trips and retrieve computed carbon footprint metrics and recommendations.

The server stores and aggregates logged data over configurable windows (week, month) to compute carbon footprint metrics and detect trends (for example, an increase in footprint if a user switches from subway to taxis). Clients can compare their footprint against global or peer averages while preserving anonymity.

See Github [Issues](https://github.com/kyra-rk/charizard/issues) page for ongoing project management!

Demo Client App: https://github.com/RhysTalley/charizard_client_app

Hosted Base URL: https://charizard-carbon-estimator-901787757680.us-east1.run.app/

## Emission Factors Architecture

The service calculates CO2 emissions for transit events using **DEFRA 2024 UK Government greenhouse gas conversion factors**. Rather than calling external APIs, factors are loaded from online sources and **persisted locally** (in-memory or MongoDB) for repeated use.

### Data Model

An `EmissionFactor` contains:
- **mode**: Transportation type (`car`, `bus`, `subway`, `train`, `bike`, `walk`, `taxi`)
- **fuel_type**: For cars/taxis only (`petrol`, `diesel`, `electric`, `hybrid`)
- **vehicle_size**: For cars/taxis only (`small`, `medium`, `large`)
- **kg_co2_per_km**: Emissions per passenger-km (well-to-wheel, includes fuel production)
- **source**: Data origin (e.g., `DEFRA-2024`, `EPA-2023`)
- **updated_at**: Epoch timestamp when last updated

### Calculation Logic

When a transit event is recorded, the service:
1. Looks up the appropriate emission factor (or loads default DEFRA factors if store is empty)
2. For **private vehicles** (car, taxi): multiplies `factor × distance ÷ occupancy` (passenger sharing reduces per-capita emissions)
3. For **public transit** (bus, subway, train): uses pre-averaged per-passenger factors (occupancy already factored in)
4. Returns total CO2 in kg CO2e

**Example:**
- Petrol medium car: 0.203 kg CO2e/km
- 10 km trip, 1 passenger: 0.203 × 10 ÷ 1 = **2.03 kg CO2e**
- 10 km trip with 2 passengers: 0.203 × 10 ÷ 2 = **1.015 kg CO2e** per person

Source: [UK Government Greenhouse Gas Reporting Conversion Factors 2024](https://www.gov.uk/guidance/greenhouse-gas-reporting-conversion-factors-2024)

## Building & Running the Service
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

# Clear entire DB (events, api_keys, logs, emission_factors) (GET /admin/clear-db)
$ curl -H "Authorization: Bearer changeme_admin_key_please_replace" http://localhost:8080/admin/clear-db
```

# List stored factors (returns persisted factors, or DEFRA defaults if store is empty)
```bash
$ curl -H "Authorization: Bearer changeme_admin_key_please_replace" \
    http://localhost:8080/admin/emission-factors | jq
```

# Load and persist DEFRA 2024 factors (loads hardcoded defaults into the store)
```bash
$ curl -X POST -H "Authorization: Bearer changeme_admin_key_please_replace" \
    http://localhost:8080/admin/emission-factors/load | jq
```

## Development Notes
Development Tools:
- [cpp-httplib](https://github.com/yhirose/cpp-httplib): An easy RESTful API library for C++ developers
- MongoDB: A document database for persistent storage of carbon footprint data
- CMake and Make: Build/dependency manager
- GoogleTest: Testing framework for unit and integration tests
- `.clang-format` and `.clang-tidy`: Lint and check style

The `Makefile` lets you use many shortcuts to conveniently build the service with different
configurations as needed. To see a full menu of available targets, run

    $ make help

To build the service, run

    $ make build

To run the executable, run

    $ make run

To run through all tests, run

    $ make test # or make test-verbose

You can also rebuild the service by running

    $ make rebuild # this will be useful when you edit something

To run the style checker, run

    $ make format-check

You can fix style issues with

    $ make format

To perform a static analysis on your code, run

    $ make lint

If your lint errors are easy enough to fix, you can run

    $ make lint-fix

To run a coverage report on your tests, run

    $ make build-cov
    $ make coverage
    $ make cov-open

As of the last time this codebase was modified, the coverage is as follows:
![Function (100%), Line (98.11%), Region (91.63%), Branch (79.23%)](assets/img/coverage.png "Coverage")

## Continuous Integration

This project uses GitHub Actions to automatically run tests, style checks, and static analysis on every pull request to the `main` branch.

### What runs automatically in CI:
- **All unit tests** — via `make test`, which includes all tests discovered by CTest
- **All API/integration tests** — automatically run as part of `make test`
- **Style checking** — via `make format-check` to ensure code follows the project's formatting standards
- **Static analysis** — via `make lint` to catch potential code issues
- **Test coverage** — via `make coverage`, which builds with coverage instrumentation and runs the test suite to generate coverage data

The CI workflow is defined in `.github/workflows/ci.yml` and runs on macOS (matching our primary development environment).

### Viewing CI Results:
1. Open a pull request to `main`
2. GitHub will automatically trigger the CI workflow
3. View the status on your PR page — green checkmark means all tests passed, red X means failures
4. Click "Details" next to a failed check to see logs and identify what went wrong

### Running CI checks locally:
Before pushing, you can run the same checks locally to catch issues early:
```bash
# Run all tests
$ make test

# Check code formatting
$ make format-check

# Run static analysis
$ make lint
```

### CI Reports:
CI run results are visible in the "Actions" tab of the GitHub repository. Each workflow run shows:
- Build logs
- Test results with pass/fail status
- Formatting and linting errors (if any)

### Static Analysis Section:

CI initially flagged tens of thousands of warnings due to an overly broad .clang-tidy rule set. These included readability and modernization suggestions that were not actionable.

The rule set was simplified to focus on actual static-analysis checks (clang-analyzer-* and bugprone-*). After fixing issues in service code and rerunning CI, no new actionable static analysis warnings appeared. Before/after CI logs are included below.

### Style Checking Section: 

Our CI pipeline originally ran strict readability and checkstyle rules that created excessive noise without improving code quality.

These were removed, and the CI loop now enforces formatting via clang-format and minimal style/lint checks. All style errors were fixed, and a clean CI report is included below.

## Testing Specifics
### `POST /users/register`
**Partitions**
- Body missing / empty
    - Test: `ApiRegister.Failure_MissingBody`
- Body not valid JSON
    - Test: `ApiRegister.Failure_InvalidJson`
- Body valid JSON but app_name missing
    - Test: `ApiRegister.Failure_MissingAppName`
- Body valid JSON but app_name wrong type
    - Test: `ApiRegister.Failure_NonStringAppName`
- Body valid JSON and app_name empty string (boundary)
    - Test: `ApiRegister.Failure_EmptyAppName`
- Body valid, app_name non-empty string
    - Tests: `ApiRegister.Success_MinimalJson`, `ApiRegister.Success_IgnoresExtraFields`
- Content-type variants
    - Test: `ApiRegister.Success_AcceptsPlainContentType`

### `POST /users/{user_id}/transit`
**Partitions**
- Path invalid
    - Test: `ApiTransit.BadPath_InvalidUserSegment`
    - No Authorization header → 401
        - Test: `ApiTransit.Unauthorized_MissingAuthHeader`
    - Wrong or malformed key → 401
        - Test: `ApiTransit.Unauthorized_WrongApiKey`
    - User not registered in store → 401 (even with an “ok” header)
        - Test: `ApiTransit.Unauthorized_UserNotRegistered`
    - Correct header & user exists → 201
        - Test: All success cases.
- Body invalid / malformed
    - Non-JSON body → invalid_json (400)
        - Test: `ApiTransit.Failure_InvalidJson`
    - JSON object missing mode → missing_fields (400)
        - Test: `ApiTransit.Failure_MissingModeField`
    - JSON object missing distance_km → missing_fields (400)
        - Test: `ApiTransit.Failure_MissingDistanceField`
- Type errors
    - mode not a string → invalid JSON payload (catch json exception)
        - Test: `ApiTransit.Failure_ModeWrongType`
    - distance_km not numeric → invalid JSON payload
        - Test: `ApiTransit.Failure_DistanceWrongType`
- Domain validation (via TransitEvent) 
    - Invalid mode (not one of car/bus/taxi/...) → error: invalid mode 400
        - Test: `ApiTransit.Failure_InvalidMode_PropagatesMessage`
    - Distance ≤ 0 – you test this at the TransitEvent level (see TransitEventTest), and the API route’s generic runtime_error → 400 path is exercised by the invalid-mode test as well.
- Happy paths / boundary on ts
    - Valid JSON, mode + distance_km, no ts → default to now_epoch()
        - Test: `ApiTransit.Success_ValidJson_DefaultTs`
    - Same but with explicit ts (boundary: “ts optional; when present, must be int64”)
        - Test: `ApiTransit.Success_ValidJson_ExplicitTs`

### `GET /users/{user_id}/lifetime-footprint`
**Partitions**
- Path invalid → 404 bad_path
    - Test: `ApiFootprint.Failure_BadPath`
- Auth
    - Missing header → 401
        - Test: `ApiFootprint.Unauthorized_MissingAuthHeader`
    - Wrong key → 401
        - Test: `ApiFootprint.Unauthorized_WrongApiKey`
    - Correct key & user exists → 200
        - All success tests.
- Events dataset
    - No events for user → footprint = 0
        - Test: `ApiFootprint.Success_NoEvents_ZeroFootprint`
    - Some events (car, train, etc.) → compute emissions
        - Tests: `ApiFootprint.Success_WithCarEvents_UsesCalculator`, `ApiFootprint.Success_WithTrain_UsesDefraFactor`
- Cache behavior
    - First call: compute & store
    - Second call: hit cache
        - Test: `ApiFootprint.Success_CacheHit`

### `GET /users/{user_id}/suggestions`
**Partitions**
- Path invalid → 404 bad_path
    - Test: ApiSuggestions.Failure_BadPath
- Auth
    - Missing / wrong → 401
        - Test: `ApiSuggestions.Unauthorized_MissingAuthHeader`
- Weekly CO₂ bucket (branch at 20.0)
    - `s.week_kg_co2 > 20.0` → “high impact” suggestions (two messages)
        - Test: `ApiSuggestions.HighFootprint_ReturnsHighImpactSuggestions`
    - `s.week_kg_co2 <= 20.0` → “low impact / keep it up” suggestion
        - Test: `ApiSuggestions.LowFootprint_ReturnsLowImpactMessage`

### `GET /users/{user_id}/analytics`
**Partitions**
- Path invalid → 404
    - Test: ApiAnalytics.Failure_BadPath
- Auth partitions (like others)
    - Missing / wrong → 401
        - Tests: `ApiAnalytics.Unauthorized_MissingAuthHeader`, `ApiAnalytics.Unauthorized_WrongApiKey`
    - Correct → 200
        - Test: `ApiAnalytics.Success_ReturnsWeeklyAndPeerStats`
- Analytics values
    - Branch: `above_peer_avg = (user_week > peer_avg)`

### `GET and DELETE /admin/logs`
- Auth:
    - Env var not set → always unauthorized
        - Test: `AdminAuth.Unauthorized_NoAdminKeyEnvVar`
    - Env var set but header missing → 401
        - Test: `AdminLogs.Unauthorized_GetLogs`
    - Header value without "Bearer " prefix → 401
        - Test: `AdminAuth.Unauthorized_MissingBearerPrefix`
    - Header with "Bearer WRONG" → 401
        - Test: `AdminLogs.Unauthorized_DeleteLogs`
    - Correct "Bearer super-secret" → 200
        - All happy tests.
- Log volume (GET)
    - n <= 1000 logs → returns all
    - n > 1000 → returns last 1000 only
        - Test: `AdminLogs.Logs_ExceedLimit_ReturnsLastN` 
- DELETE /admin/logs
    - With logs present → clears and subsequent GET shows empty array
        - Test: `AdminLogs.DeleteLogs_ClearsAll`

### `/admin/clients` and `/admin/clients/{id}/data`
- Auth
    - Missing/wrong header → 401
        - Tests: `AdminAuth...`, `AdminLogs.Unauthorized_GetLogs`, etc.
    - Correct header → 200.
- GET /admin/clients
    - No active clients (no events) → empty array
    - At least one client with events → array includes "demo"
        - Test: `AdminClients.Clients_ListContainsDemoAfterTransit`
- GET /admin/clients/{id}/data
    - Path invalid (regex failure) → 404 bad_path
    - id exists and has events → non-empty event array
        - Tests: `AdminClients.ClientData_ReturnsUserTransitEvents_Count`, `AdminClients.ClientData_ReturnsUserTransitEvents_Content`
    - id does not exist → empty array
        - Test: `AdminClients.ClientData_UnknownUser_ReturnsEmptyArray`

### `/admin/clear-db-events` and `/admin/clear-db`
- Auth
    - Wrong admin header → 401
        - Testss: `AdminDb.Unauthorized_ClearDbEvents`, `AdminDb.Unauthorized_ClearDb`
    - Correct header → 200 and status "ok"
- State effects
    - ClearDbEvents after seeding events:
        - Before: /admin/clients non-empty
        - After: /admin/clients empty
        - Test: `AdminDb.ClearDbEvents_RemovesOnlyEvents_AfterwardsClientsEmpty`
    - ClearDb after events + logs:
        - After: /admin/clients empty and /admin/logs empty
        - Test: `AdminDb.ClearDb_RemovesEverything_ClientsEmpty`

### `/admin/emission-factors` and `/admin/emission-factors/load`
- Auth
    - Wrong / missing admin → 401
    - Correct header → 200.
- GET /admin/emission-factors
    - Store has no persisted emission factors → returns DEFRA defaults from loader
        - Test: `AdminEmissionFactors.GetDefaults_ReturnsBasicDefaults`
    - Store has persisted factors → returns persisted set
        - Test: `AdminEmissionFactors.LoadDefra2024_ReturnsCount`
- POST /admin/emission-factors/load
    - Correct admin header; body ignored except for content type → loads DEFRA factors and returns count
        - Test: `AdminEmissionFactors.LoadDefra2024_ReturnsCount`

----
*Last updated December 4, 2025*
