#!/bin/zsh

# Color codes for script output
RED=$'\e[0;31m'
GREEN=$'\e[0;32m'
YELLOW=$'\e[1;33m'
BLUE=$'\e[0;34m'
MAGENTA=$'\e[0;35m'
CYAN=$'\e[0;36m'
WHITE=$'\e[0;37m'
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

demo_step() {
    local summary="$1"
    local cmd="$2"

    echo ""
    echo "${BOLD}${CYAN}=== STEP: $summary ===${RESET}"
    echo "${YELLOW}$cmd${RESET}"
    echo -n "${GREEN}Continue? (Y/n): ${RESET}"
    read answer
    # Default Yes on empty; explicit n/N to skip
    if [[ -z "$answer" || "$answer" == [Yy]* ]]; then
        echo "${MAGENTA}Running...${RESET}"
        eval "$cmd"
        echo "${GREEN}✓ Completed${RESET}"
    else
        echo "${RED}Skipped${RESET}"
    fi
}

# Step 1: Health check
demo_step "Health check" "
    curl -s '$BASE_URL/health' | jq .
"

# Step 2: Register user
demo_step "Register new client app" "
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

echo "${GREEN}Registered: user_id=$USER_ID, api_key=${API_KEY:0:8}...${RESET}"

# Step 3-5: Transit logs
demo_step "Log 5km car trip" "
    curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
        -H 'Content-Type: application/json' \\
        -H 'X-API-Key: $API_KEY' \\
        -d '{\"mode\":\"car\",\"distance_km\":5.2}' | jq .
"

demo_step "Log 3km subway trip" "
    curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
        -H 'Content-Type: application/json' \\
        -H 'X-API-Key: $API_KEY' \\
        -d '{\"mode\":\"subway\",\"distance_km\":3.1}' | jq .
"

demo_step "Log 12km train trip" "
    curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
        -H 'Content-Type: application/json' \\
        -H 'X-API-Key: $API_KEY' \\
        -d '{\"mode\":\"train\",\"distance_km\":12.4}' | jq .
"

# Step 6: Show admin logs
demo_step "Show request logs (admin)" "
    curl -s -H 'Authorization: Bearer $ADMIN_KEY' \\
        '$BASE_URL/admin/logs' | jq .
"

# Step 7: Show client data
demo_step "Show client data (admin)" "
    curl -s -H 'Authorization: Bearer $ADMIN_KEY' \\
        '$BASE_URL/admin/clients/$USER_ID/data' | jq .
"

# Step 8: Check lifetime footprint
demo_step "Check lifetime footprint" "
    curl -s -H 'X-API-Key: $API_KEY' \\
        '$BASE_URL/users/$USER_ID/lifetime-footprint' | jq .
"

# Step 9: Clear DB events (logs stay, events go)
demo_step "Clear events only (logs preserved)" "
    curl -s -X GET -H 'Authorization: Bearer $ADMIN_KEY' \\
        '$BASE_URL/admin/clear-db-events' | jq .
"

echo "${CYAN}After clearing events, /admin/clients should now return an empty array ([]).${RESET}"
demo_step "Check /admin/clients returns empty array" "
    curl -s -H 'Authorization: Bearer $ADMIN_KEY' \\
        '$BASE_URL/admin/clients' | jq .
"

echo "${CYAN}Because lifetime footprint is cached, the next call may still show the OLD value (not 0) until we log new trips.${RESET}"
demo_step "Check lifetime footprint (still using cached value)" "
    curl -s -H 'X-API-Key: $API_KEY' \\
        '$BASE_URL/users/$USER_ID/lifetime-footprint' | jq .
"

# Step 10: New events will invalidate/recompute cache and lower the values
demo_step "Log another 8.7km taxi trip (triggers cache recompute)" "
    curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
        -H 'Content-Type: application/json' \\
        -H 'X-API-Key: $API_KEY' \\
        -d '{\"mode\":\"taxi\",\"distance_km\":8.7}' | jq .
"

demo_step "Log 4.2km bike trip" "
    curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
        -H 'Content-Type: application/json' \\
        -H 'X-API-Key: $API_KEY' \\
        -d '{\"mode\":\"bike\",\"distance_km\":4.2}' | jq .
"

echo "${CYAN}Now lifetime footprint should be recomputed from only the NEW events, so the numbers should drop compared to the cached pre-clear value.${RESET}"
demo_step "Check updated lifetime footprint (after cache invalidation)" "
    curl -s -H 'X-API-Key: $API_KEY' \\
        '$BASE_URL/users/$USER_ID/lifetime-footprint' | jq .
"

# Step 10: New events + analytics/suggestions (still using current factors)
demo_step "Log another 8.7km taxi trip" "
    curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
        -H 'Content-Type: application/json' \\
        -H 'X-API-Key: $API_KEY' \\
        -d '{\"mode\":\"taxi\",\"distance_km\":8.7}' | jq .
"

demo_step "Log 4.2km bike trip" "
    curl -s -X POST '$BASE_URL/users/$USER_ID/transit' \\
        -H 'Content-Type: application/json' \\
        -H 'X-API-Key: $API_KEY' \\
        -d '{\"mode\":\"bike\",\"distance_km\":4.2}' | jq .
"

demo_step "Check updated lifetime footprint" "
    curl -s -H 'X-API-Key: $API_KEY' \\
        '$BASE_URL/users/$USER_ID/lifetime-footprint' | jq .
"

demo_step "View analytics (peer comparison)" "
    curl -s -H 'X-API-Key: $API_KEY' \\
        '$BASE_URL/users/$USER_ID/analytics' | jq .
"

demo_step "View suggestions" "
    curl -s -H 'X-API-Key: $API_KEY' \\
        '$BASE_URL/users/$USER_ID/suggestions' | jq .
"

echo ""
echo "${BOLD}${GREEN}=== Demo Complete! ===${RESET}"
echo "${CYAN}Summary:${RESET}"
echo "- User registered: $USER_ID"
echo "- API Key: ${API_KEY:0:8}... (save this!)"
echo "- Server running at: $BASE_URL"
echo ""
echo "${YELLOW}To clean up everything: curl -H 'Authorization: Bearer $ADMIN_KEY' '$BASE_URL/admin/clear-db'${RESET}"
