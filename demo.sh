#!/bin/zsh

# Define -a flag for admin script
ADMIN_MODE=0
if [[ "$1" == "-a" ]]; then
  ADMIN_MODE=1
fi

# Color codes for script output
RED=$'\e[0;31m'
GREEN=$'\e[0;32m'
YELLOW=$'\e[1;33m'
BLUE=$'\e[0;34m'
MAGENTA=$'\e[0;35m'
CYAN=$'\e[0;36m'
WHITE=$'\e[0;37m'
COMMENT=$'\e[2;37m'
BOLD=$'\e[1m'
RESET=$'\e[0m'

# Base URL for the local server
BASE_URL="http://localhost:8080"
ADMIN_KEY="changeme_admin_key_please_replace"

echo "${BOLD}${YELLOW}=== Charizard Carbon Estimator Demo Script ===${RESET}"

echo ""
echo "${CYAN}IMPORTANT: Before running this script, start the server in a separate terminal:${RESET}"
echo "${GREEN}  export ADMIN_API_KEY=changeme_admin_key_please_replace && make build && make run${RESET}"
echo "${YELLOW}Press Enter when server is running (check http://localhost:8080/health)...${RESET}"
read

# Pretty printing
STEP_NUM=0

hr() { print -r -- "${WHITE}────────────────────────────────────────────────────────${RESET}"; }

section() {
  local title="$1"
  echo ""
  hr
  echo "${BOLD}${MAGENTA}${title}${RESET}"
  hr
}

demo_step_verbose() {
    local summary="$1"
    local cmd="$2"
    ((STEP_NUM++))

    echo ""
    echo "${BOLD}${CYAN}=== STEP ${STEP_NUM}: $summary ===${RESET}"
    echo "${YELLOW}$cmd${RESET}"
    echo -n "${GREEN}Continue? (Y/n): ${RESET}"
    read answer
    echo ""
    if [[ -z "$answer" || "$answer" == [Yy]* ]]; then
        eval "$cmd"
    else
        echo "${RED}Skipped${RESET}"
    fi
}

demo_step_quiet() {
    local cmd="$1"
    eval "$cmd" >/dev/null 2>&1
}

# Wrapper: user steps are verbose normally, quiet when -a is used
demo_step_user() {
    local summary="$1"
    local cmd="$2"

    if [[ $ADMIN_MODE -eq 1 ]]; then
        demo_step_quiet "$cmd"
    else
        demo_step_verbose "$summary" "$cmd"
    fi
}

# Admin steps only run when -a is used (and they are verbose)
demo_step_admin() {
    local summary="$1"
    local cmd="$2"

    demo_step_verbose "$summary" "$cmd"
}

# User endpoints
if [[ $ADMIN_MODE -eq 1 ]]; then
  section "Running through user endpoints"
fi

demo_step_user "Health check" "
  curl -s '$BASE_URL/health' | jq .
"

demo_step_user "Register new client app" "
  curl -s -X POST '$BASE_URL/users/register' \\
    -H 'Content-Type: application/json' \\
    -d '{\"app_name\":\"demo-app\"}' | jq . | tee /tmp/register.json
"

USER_ID=$(jq -r '.user_id' /tmp/register.json 2>/dev/null || echo "")
API_KEY=$(jq -r '.api_key' /tmp/register.json 2>/dev/null || echo "")

if [[ -z $USER_ID || -z $API_KEY ]]; then
    echo "${RED}Registration failed! Exiting.${RESET}"
    exit 1
fi

if [[ $ADMIN_MODE -eq 0 ]]; then
  echo "${GREEN}Registered: user_id=$USER_ID, api_key=${API_KEY:0:8}...${RESET}"
fi

demo_step_user "Log 5.2km car trip" "
  curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
    -H 'Content-Type: application/json' \\
    -H 'X-API-Key: $API_KEY' \\
    -d '{\"mode\":\"car\",\"distance_km\":5.2}' | jq .
"

demo_step_user "Log 3.1km subway trip" "
  curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
    -H 'Content-Type: application/json' \\
    -H 'X-API-Key: $API_KEY' \\
    -d '{\"mode\":\"subway\",\"distance_km\":3.1}' | jq .
"

demo_step_user "Log 12.4km train trip" "
  curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
    -H 'Content-Type: application/json' \\
    -H 'X-API-Key: $API_KEY' \\
    -d '{\"mode\":\"train\",\"distance_km\":12.4}' | jq .
"

demo_step_user "Check lifetime footprint" "
  curl -s -H 'X-API-Key: $API_KEY' \\
    '$BASE_URL/users/$USER_ID/lifetime-footprint' | jq .
"

demo_step_user "View analytics (peer comparison)" "
  curl -s -H 'X-API-Key: $API_KEY' \\
    '$BASE_URL/users/$USER_ID/analytics' | jq .
"

demo_step_user "View suggestions" "
  curl -s -H 'X-API-Key: $API_KEY' \\
    '$BASE_URL/users/$USER_ID/suggestions' | jq .
"

demo_step_user "Log 100km train trip (high carbon impact)" "
  curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
    -H 'Content-Type: application/json' \\
    -H 'X-API-Key: $API_KEY' \\
    -d '{\"mode\":\"train\",\"distance_km\":100}' | jq .
"

demo_step_user "View suggestions after long-haul train ride" "
  curl -s -H 'X-API-Key: $API_KEY' \\
    '$BASE_URL/users/$USER_ID/suggestions' | jq .
"

# Exit if user-only script
if [[ $ADMIN_MODE -eq 0 ]]; then
  echo ""
  echo "${BOLD}${GREEN}=== Demo Complete! (User endpoints only) ===${RESET}"
  echo "${CYAN}Summary:${RESET}"
  echo "- User registered: $USER_ID"
  echo "- API Key: ${API_KEY:0:8}... (save this!)"
  echo "- Server running at: $BASE_URL"
  exit 0
fi

# Admin endpoints
section "Admin endpoints"

demo_step_admin "Show request logs (admin)" "
  curl -s -H 'Authorization: Bearer $ADMIN_KEY' \\
    '$BASE_URL/admin/logs' | jq .
"

demo_step_admin "Show client data (admin)" "
  curl -s -H 'Authorization: Bearer $ADMIN_KEY' \\
    '$BASE_URL/admin/clients/$USER_ID/data' | jq .
"

demo_step_admin "Clear events only (logs preserved)" "
  curl -s -X GET -H 'Authorization: Bearer $ADMIN_KEY' \\
    '$BASE_URL/admin/clear-db-events' | jq .
"

echo "${COMMENT}After clearing events, /admin/clients should now return an empty array ([]).${RESET}"
demo_step_admin "Check /admin/clients returns empty array" "
  curl -s -H 'Authorization: Bearer $ADMIN_KEY' \\
    '$BASE_URL/admin/clients' | jq .
"

echo "${COMMENT}Because lifetime footprint is cached, the next call may still show the OLD value (not 0) until we log new trips.${RESET}"
demo_step_admin "Check lifetime footprint (still using cached value)" "
  curl -s -H 'X-API-Key: $API_KEY' \\
    '$BASE_URL/users/$USER_ID/lifetime-footprint' | jq .
"

demo_step_admin "Log another 8.7km taxi trip (triggers cache recompute)" "
  curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
    -H 'Content-Type: application/json' \\
    -H 'X-API-Key: $API_KEY' \\
    -d '{\"mode\":\"taxi\",\"distance_km\":8.7}' | jq .
"

demo_step_admin "Log 4.2km bike trip" "
  curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
    -H 'Content-Type: application/json' \\
    -H 'X-API-Key: $API_KEY' \\
    -d '{\"mode\":\"bike\",\"distance_km\":4.2}' | jq .
"

echo "${COMMENT}Now lifetime footprint should be recomputed from only the NEW events, so the numbers should drop compared to the cached pre-clear value.${RESET}"
demo_step_admin "Check updated lifetime footprint (after cache invalidation)" "
  curl -s -H 'X-API-Key: $API_KEY' \\
    '$BASE_URL/users/$USER_ID/lifetime-footprint' | jq .
"

echo ""
echo "${BOLD}${GREEN}=== Demo Complete! (User + Admin) ===${RESET}"
echo "${CYAN}Summary:${RESET}"
echo "- User registered: $USER_ID"
echo "- API Key: ${API_KEY:0:8}... (save this!)"
echo "- Server running at: $BASE_URL"
echo ""
echo "${YELLOW}To clean up everything: curl -H 'Authorization: Bearer $ADMIN_KEY' '$BASE_URL/admin/clear-db'${RESET}"
