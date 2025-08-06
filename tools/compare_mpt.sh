#!/bin/bash

# requires:
# npm install @ethereumjs/trie @ethereumjs/rlp ethereum-cryptography
# cmake --build build -j --target mptCompareCpp

# MPT Comparison Script - Compare C++ and JavaScript MPT implementations
# Usage: ./compare_mpt.sh <json_file>

if [ $# -ne 1 ]; then
    echo "Usage: $0 <json_file>"
    echo "Example: $0 test_accounts.json"
    exit 1
fi

JSON_FILE="$1"

if [ ! -f "$JSON_FILE" ]; then
    echo "Error: File '$JSON_FILE' not found!"
    exit 1
fi

echo "=== MPT Implementation Comparison ==="
echo "Input file: $JSON_FILE"
echo ""

# Run C++ implementation
echo "--- C++ Implementation ---"
CPP_OUTPUT=$(./build/mptCompareCpp "$JSON_FILE")
CPP_EXIT_CODE=$?

if [ $CPP_EXIT_CODE -ne 0 ]; then
    echo "ERROR: C++ implementation failed"
    echo "$CPP_OUTPUT"
    exit 1
fi

echo "$CPP_OUTPUT" > /tmp/cpp_result.json

# Run JavaScript implementation
echo "--- JavaScript Implementation ---"
JS_OUTPUT=$(node ./tools/mpt_compare_js.js "$JSON_FILE")
JS_EXIT_CODE=$?

if [ $JS_EXIT_CODE -ne 0 ]; then
    echo "ERROR: JavaScript implementation failed"
    echo "$JS_OUTPUT"
    exit 1
fi

echo "$JS_OUTPUT" > /tmp/js_result.json

# Extract state roots for comparison
CPP_STATE_ROOT=$(echo "$CPP_OUTPUT" | grep -o '"stateRoot"[[:space:]]*:[[:space:]]*"[^"]*"' | sed 's/.*"\([^"]*\)"/\1/')
JS_STATE_ROOT=$(echo "$JS_OUTPUT" | grep -o '"stateRoot"[[:space:]]*:[[:space:]]*"[^"]*"' | sed 's/.*"\([^"]*\)"/\1/')

echo ""
echo "=== Results Comparison ==="
echo "C++ State Root:  $CPP_STATE_ROOT"
echo "JS State Root:   $JS_STATE_ROOT"

if [ "$CPP_STATE_ROOT" = "$JS_STATE_ROOT" ]; then
    echo "✅ SUCCESS: State roots match!"
    echo ""

    # Compare detailed account processing
    echo "=== Detailed Account Comparison ==="

    # Extract and compare account data using jq if available
    if command -v jq &> /dev/null; then
        echo "Comparing account details..."

        # Compare each account's processed data
        ACCOUNTS_MATCH=true

        for account in $(echo "$CPP_OUTPUT" | jq -r '.accounts[].address'); do
            CPP_ACCOUNT=$(echo "$CPP_OUTPUT" | jq ".accounts[] | select(.address == \"$account\")")
            JS_ACCOUNT=$(echo "$JS_OUTPUT" | jq ".accounts[] | select(.address == \"$account\")")

            CPP_HASH=$(echo "$CPP_ACCOUNT" | jq -r '.addressHash')
            JS_HASH=$(echo "$JS_ACCOUNT" | jq -r '.addressHash')

            if [ "$CPP_HASH" != "$JS_HASH" ]; then
                echo "❌ Address hash mismatch for $account"
                echo "   C++: $CPP_HASH"
                echo "   JS:  $JS_HASH"
                ACCOUNTS_MATCH=false
            fi

            CPP_RLP=$(echo "$CPP_ACCOUNT" | jq -r '.accountRLP')
            JS_RLP=$(echo "$JS_ACCOUNT" | jq -r '.accountRLP')

            if [ "$CPP_RLP" != "$JS_RLP" ]; then
                echo "❌ Account RLP mismatch for $account"
                echo "   C++: $CPP_RLP"
                echo "   JS:  $JS_RLP"
                ACCOUNTS_MATCH=false
            fi
        done

        if [ "$ACCOUNTS_MATCH" = true ]; then
            echo "✅ All account details match perfectly!"
        fi
    else
        echo "Note: Install 'jq' for detailed account comparison"
    fi
else
    echo "❌ FAILURE: State roots do not match!"

    echo ""
    echo "=== Debug Information ==="
    echo "C++ Output:"
    echo "$CPP_OUTPUT"
    echo ""
    echo "JavaScript Output:"
    echo "$JS_OUTPUT"
fi

echo ""
echo "=== Summary ==="
if [ "$CPP_STATE_ROOT" = "$JS_STATE_ROOT" ]; then
    echo "✅ Both implementations produce identical results"
    exit 0
else
    echo "❌ Implementations produce different results"
    exit 1
fi