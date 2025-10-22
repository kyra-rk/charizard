# Carbon Estimator
An API service that accurately estimates a user's carbon footprint.

## Development
Development Tools:
- [cpp-httplib](https://github.com/yhirose/cpp-httplib): An easy RESTful API library for C++ developers
- MongoDB: A document database for persistent storage of carbon footprint data
- CMake and Make: Build/dependency manager
- GoogleTest: Testing framework for unit and integration tests
- `.clang-format` and `.clang-tidy`: Lint and check style

## Running the Service
Running the program is simple:
    $ brew bundle
    $ make build && make run

From there, you can send the service API requests via `curl` or any tool of your choice. For example,

    $ curl -s http://localhost:8080/health | jq
    {
       "ok": true,
       "service": "charizard",
       "time": 1761091516
    }

## Development
The `Makefile` lets you use many shortcuts to conveniently build the service with different
configurations as needed. To see a full menu of available targets, run

    $ make help

To build the service, run

    make build

To run the executable, run

    make run

To run through tests, run

    make test # or make test-verbose

You can also rebuild the service by running

    make rebuild # this will be useful when you edit something

To run the style checker, run

    make format

To perform a static analysis on your code, run

    make lint

If your lint errors are easy enough to fix, you can run

    make lint-fix