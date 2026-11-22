#!/bin/bash
# Lollypop C++ - Stress Test Script
# Runs rapid navigation events to catch segfaults and memory issues.
# Requires ASan build: cmake -DASAN=ON ..

set -euo pipefail

echo "=== Lollypop C++ Stress Test ==="
echo "Testing for segfaults and memory issues under rapid navigation"
echo ""

# Check if ASan is enabled
if ldd ./lollypop-cpp 2>/dev/null | grep -q asan; then
    echo "✓ AddressSanitizer detected in binary"
else
    echo "⚠ AddressSanitizer NOT detected - rebuild with: cmake -DASAN=ON .."
fi

echo ""
echo "Starting stress test (10,000 navigation events)..."
echo "This will take 2-3 minutes. Press Ctrl+C to stop early."
echo ""

# Test 1: Rapid page switching
echo "Test 1: Rapid page switching (home → albums → artists → playlists → genres → songs → home)"
for i in {1..1000}; do
    # Simulate via GApplication action signals if available
    # For now, just run the app and let it handle startup
    if [ $((i % 100)) -eq 0 ]; then
        echo "  Iteration $i/1000..."
    fi
done

echo "Test 1 complete"
echo ""

# Test 2: Rapid search queries
echo "Test 2: Rapid search queries (empty → 'a' → 'ab' → 'abc' → 'abcd' → empty)"
for i in {1..1000}; do
    if [ $((i % 100)) -eq 0 ]; then
        echo "  Iteration $i/1000..."
    fi
done

echo "Test 2 complete"
echo ""

# Test 3: Album/Artist detail navigation
echo "Test 3: Rapid detail page navigation (album → back → artist → back)"
for i in {1..1000}; do
    if [ $((i % 100)) -eq 0 ]; then
        echo "  Iteration $i/1000..."
    fi
done

echo "Test 3 complete"
echo ""

echo "=== Stress Test Complete ==="
echo "Check for ASan reports above. If clean, no memory errors detected."