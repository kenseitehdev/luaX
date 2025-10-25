-- test_require.lua
-- Test script for C module loading via the Lua C API compatibility layer

print("=== LuaX C Module Compatibility Test ===")
print()

-- Test 1: Load the test module
print("Test 1: Loading test module...")
local ok, test = pcall(require, "test")
if not ok then
    print("ERROR loading module: " .. tostring(test))
    print()
    print("Make sure you built the test module with:")
    print("  make test-module-mac")
    os.exit(1)
end
print("✓ SUCCESS: Module loaded")
print("  Module type: " .. type(test))
print()

-- Test 2: Call simple function
print("Test 2: Testing simple addition...")
local result = test.add(5, 7)
print("  test.add(5, 7) = " .. result)
if result ~= 12 then
    print("✗ FAILED: Expected 12, got " .. result)
    os.exit(1)
end
print("✓ SUCCESS")
print()

-- Test 3: Multiple return values
print("Test 3: Testing multiple return values...")
local a, b, c = test.multi()
print("  test.multi() returned:")
print("    a = " .. tostring(a) .. " (type: " .. type(a) .. ")")
print("    b = " .. tostring(b) .. " (type: " .. type(b) .. ")")
print("    c = " .. tostring(c) .. " (type: " .. type(c) .. ")")
if a ~= "hello" or b ~= 42 or c ~= true then
    print("✗ FAILED: Unexpected return values")
    os.exit(1)
end
print("✓ SUCCESS")
print()

-- Test 4: Table operations
print("Test 4: Testing table operations...")
local sum = test.tablesum({1, 2, 3, 4, 5})
print("  test.tablesum({1, 2, 3, 4, 5}) = " .. sum)
if sum ~= 15 then
    print("✗ FAILED: Expected 15, got " .. sum)
    os.exit(1)
end
print("✓ SUCCESS")
print()

-- Test 5: Creating tables
print("Test 5: Testing table creation...")
local person = test.makeperson("Alice", 30)
print("  test.makeperson('Alice', 30) returned:")
print("    name = " .. tostring(person.name))
print("    age = " .. tostring(person.age))
if person.name ~= "Alice" or person.age ~= 30 then
    print("✗ FAILED: Unexpected table contents")
    os.exit(1)
end
print("✓ SUCCESS")
print()

-- Test 6: String concatenation
print("Test 6: Testing string concatenation...")
local concat_result = test.concat("Hello", " World")
print("  test.concat('Hello', ' World') = '" .. concat_result .. "'")
if concat_result ~= "Hello World" then
    print("✗ FAILED: Expected 'Hello World', got '" .. concat_result .. "'")
    os.exit(1)
end
print("✓ SUCCESS")
print()

-- Test 7: Module metadata
print("Test 7: Checking module metadata...")
print("  Version: " .. tostring(test._VERSION))
print("  Description: " .. tostring(test._DESCRIPTION))
if test._VERSION ~= "1.0.0" then
    print("✗ FAILED: Unexpected version")
    os.exit(1)
end
print("✓ SUCCESS")
print()

-- Test 8: Error handling
print("Test 8: Testing error handling...")
local err_ok, err_msg = pcall(test.error, "custom message")
if err_ok then
    print("✗ FAILED: Expected error but none was raised")
    os.exit(1)
end
print("  Error correctly raised: " .. tostring(err_msg))
print("✓ SUCCESS")
print()

print("========================================")
print("✓✓✓ ALL TESTS PASSED! ✓✓✓")
print("========================================")
print()
print("The Lua C API compatibility layer is working correctly!")
print("You can now use LuaRocks C modules with your interpreter.")
print()

-- Optional: Try loading a real LuaRocks module if available
print("Bonus: Attempting to load a LuaRocks module...")
local socket_ok, socket = pcall(require, "socket")
if socket_ok then
    print("✓ SUCCESS: luasocket loaded!")
    print("  Socket version: " .. tostring(socket._VERSION))
else
    print("  Info: luasocket not available (this is okay)")
    print("  To test with real modules:")
    print("    1. luarocks install luasocket --local")
    print("    2. export LUA_CPATH='./?.dylib;~/.luarocks/lib/lua/5.1/?.so'")
    print("    3. ./bin/luaX-macos test_require.lua")
end
print()
