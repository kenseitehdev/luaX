-- LuaX Comprehensive Test Suite
-- Tests core language features to ensure nothing broke during refactoring

print("=== LuaX Test Suite ===\n")

local passed = 0
local failed = 0

local function test(name, fn)
    io.write(name .. "... ")
    local ok, err = pcall(fn)
    if ok then
        print("✓ PASS")
        passed = passed + 1
    else
        print("✗ FAIL: " .. tostring(err))
        failed = failed + 1
    end
end

-- Basic values and types
test("nil type", function()
    assert(type(nil) == "nil")
end)

test("boolean types", function()
    assert(type(true) == "boolean")
    assert(type(false) == "boolean")
end)

test("number types", function()
    assert(type(42) == "number")
    assert(type(3.14) == "number")
    assert(type(-100) == "number")
end)

test("string type", function()
    assert(type("hello") == "string")
    assert(type('world') == "string")
end)

-- Arithmetic
test("addition", function()
    assert(2 + 3 == 5)
    assert(2.5 + 2.5 == 5.0)
end)

test("subtraction", function()
    assert(10 - 3 == 7)
    assert(5 - 5 == 0)
end)

test("multiplication", function()
    assert(4 * 5 == 20)
    assert(2.5 * 2 == 5.0)
end)

test("division", function()
    assert(10 / 2 == 5)
    assert(9 / 2 == 4.5)
end)

test("modulo", function()
    assert(10 % 3 == 1)
    assert(20 % 5 == 0)
end)

test("exponentiation", function()
    assert(2 ^ 3 == 8)
    assert(5 ^ 2 == 25)
end)

test("negation", function()
    assert(-5 == -5)
    assert(-(-5) == 5)
end)

-- Comparison
test("equality", function()
    assert(5 == 5)
    assert("hello" == "hello")
    assert(not (5 == 6))
end)

test("inequality", function()
    assert(5 ~= 6)
    assert(not (5 ~= 5))
end)

test("less than", function()
    assert(3 < 5)
    assert(not (5 < 3))
    assert(not (5 < 5))
end)

test("less than or equal", function()
    assert(3 <= 5)
    assert(5 <= 5)
    assert(not (6 <= 5))
end)

test("greater than", function()
    assert(5 > 3)
    assert(not (3 > 5))
end)

test("greater than or equal", function()
    assert(5 >= 3)
    assert(5 >= 5)
    assert(not (3 >= 5))
end)

-- Logical operators
test("and operator", function()
    assert(true and true)
    assert(not (true and false))
    assert(not (false and true))
end)

test("or operator", function()
    assert(true or false)
    assert(false or true)
    assert(not (false or false))
end)

test("not operator", function()
    assert(not false)
    assert(not (not true))
end)

-- String operations
test("string concatenation", function()
    assert("hello" .. " " .. "world" == "hello world")
    assert("foo" .. "bar" == "foobar")
end)

test("string length", function()
    assert(#"hello" == 5)
    assert(#"" == 0)
end)

-- Variables
test("local variables", function()
    local x = 10
    assert(x == 10)
    x = 20
    assert(x == 20)
end)

test("global variables", function()
    test_global = 42
    assert(test_global == 42)
    test_global = nil
end)

-- Tables
test("table creation", function()
    local t = {}
    assert(type(t) == "table")
end)

test("table indexing", function()
    local t = {a = 1, b = 2}
    assert(t.a == 1)
    assert(t["b"] == 2)
end)

test("table assignment", function()
    local t = {}
    t.x = 10
    t["y"] = 20
    assert(t.x == 10)
    assert(t.y == 20)
end)

test("array-like tables", function()
    local arr = {10, 20, 30}
    assert(arr[1] == 10)
    assert(arr[2] == 20)
    assert(arr[3] == 30)
end)

test("table length", function()
    local arr = {1, 2, 3, 4, 5}
    assert(#arr == 5)
end)

-- Functions
test("function definition", function()
    local function add(a, b)
        return a + b
    end
    assert(add(2, 3) == 5)
end)

test("function with multiple returns", function()
    local function multi()
        return 1, 2, 3
    end
    local a, b, c = multi()
    assert(a == 1 and b == 2 and c == 3)
end)

test("anonymous functions", function()
    local f = function(x) return x * 2 end
    assert(f(5) == 10)
end)

test("closures", function()
    local function counter()
        local i = 0
        return function()
            i = i + 1
            return i
        end
    end
    local c = counter()
    assert(c() == 1)
    assert(c() == 2)
    assert(c() == 3)
end)

-- Control flow
test("if statement", function()
    local x = 10
    local result
    if x > 5 then
        result = "big"
    else
        result = "small"
    end
    assert(result == "big")
end)

test("if-elseif-else", function()
    local function classify(n)
        if n < 0 then
            return "negative"
        elseif n == 0 then
            return "zero"
        else
            return "positive"
        end
    end
    assert(classify(-5) == "negative")
    assert(classify(0) == "zero")
    assert(classify(5) == "positive")
end)

test("while loop", function()
    local sum = 0
    local i = 1
    while i <= 5 do
        sum = sum + i
        i = i + 1
    end
    assert(sum == 15)
end)

test("repeat-until loop", function()
    local sum = 0
    local i = 1
    repeat
        sum = sum + i
        i = i + 1
    until i > 5
    assert(sum == 15)
end)

test("numeric for loop", function()
    local sum = 0
    for i = 1, 5 do
        sum = sum + i
    end
    assert(sum == 15)
end)

test("numeric for with step", function()
    local sum = 0
    for i = 2, 10, 2 do
        sum = sum + i
    end
    assert(sum == 30)  -- 2+4+6+8+10
end)

test("generic for with ipairs", function()
    local arr = {10, 20, 30}
    local sum = 0
    for i, v in ipairs(arr) do
        sum = sum + v
    end
    assert(sum == 60)
end)

test("generic for with pairs", function()
    local t = {a = 1, b = 2, c = 3}
    local sum = 0
    for k, v in pairs(t) do
        sum = sum + v
    end
    assert(sum == 6)
end)

test("break statement", function()
    local sum = 0
    for i = 1, 10 do
        if i > 5 then break end
        sum = sum + i
    end
    assert(sum == 15)  -- 1+2+3+4+5
end)

-- Standard library functions
test("print function", function()
    -- Just make sure it doesn't crash
    print("test")
end)

test("type function", function()
    assert(type(nil) == "nil")
    assert(type(true) == "boolean")
    assert(type(42) == "number")
    assert(type("hi") == "string")
    assert(type({}) == "table")
    assert(type(print) == "function")
end)

test("tonumber", function()
    assert(tonumber("123") == 123)
    assert(tonumber("3.14") == 3.14)
    assert(tonumber("invalid") == nil)
end)

test("tostring", function()
    assert(tostring(123) == "123")
    assert(tostring(true) == "true")
    assert(tostring(nil) == "nil")
end)

test("assert", function()
    assert(true)
    assert(1 == 1)
    local ok, err = pcall(function() assert(false) end)
    assert(not ok)
end)

test("error and pcall", function()
    local function will_error()
        error("test error")
    end
    local ok, err = pcall(will_error)
    assert(not ok)
end)

test("select", function()
    assert(select("#", 1, 2, 3) == 3)
    assert(select(2, "a", "b", "c") == "b")
end)

-- Table library
test("pairs iteration", function()
    local t = {a=1, b=2, c=3}
    local count = 0
    for k, v in pairs(t) do
        count = count + 1
    end
    assert(count == 3)
end)

test("ipairs iteration", function()
    local arr = {10, 20, 30}
    local sum = 0
    for i, v in ipairs(arr) do
        sum = sum + v
    end
    assert(sum == 60)
end)

test("next function", function()
    local t = {a=1, b=2}
    local k, v = next(t)
    assert(k ~= nil and v ~= nil)
end)

-- Metatables
test("setmetatable and getmetatable", function()
    local t = {}
    local mt = {__index = {x = 10}}
    setmetatable(t, mt)
    assert(getmetatable(t) == mt)
    assert(t.x == 10)
end)

test("__add metamethod", function()
    local t1 = {value = 5}
    local t2 = {value = 3}
    local mt = {
        __add = function(a, b)
            return {value = a.value + b.value}
        end
    }
    setmetatable(t1, mt)
    setmetatable(t2, mt)
    local result = t1 + t2
    assert(result.value == 8)
end)

test("__tostring metamethod", function()
    local t = {name = "test"}
    local mt = {
        __tostring = function(self)
            return "Table: " .. self.name
        end
    }
    setmetatable(t, mt)
    assert(tostring(t) == "Table: test")
end)

-- Varargs
test("varargs in functions", function()
    local function sum(...)
        local total = 0
        for i, v in ipairs({...}) do
            total = total + v
        end
        return total
    end
    assert(sum(1, 2, 3, 4, 5) == 15)
end)

-- Multiple assignment
test("multiple assignment", function()
    local a, b, c = 1, 2, 3
    assert(a == 1 and b == 2 and c == 3)
end)

test("swapping with multiple assignment", function()
    local a, b = 10, 20
    a, b = b, a
    assert(a == 20 and b == 10)
end)

-- Load and loadfile (if implemented)
test("load function", function()
    local f = load("return 2 + 3")
    if f then
        assert(f() == 5)
    else
        print("load not fully implemented, skipping")
    end
end)

-- Raw table access
test("rawget", function()
    local t = {x = 10}
    assert(rawget(t, "x") == 10)
end)

test("rawset", function()
    local t = {}
    rawset(t, "y", 20)
    assert(t.y == 20)
end)

test("rawequal", function()
    assert(rawequal(5, 5))
    assert(not rawequal(5, 6))
end)

-- Edge cases
test("division by zero", function()
    local result = 1 / 0
    -- Just make sure it doesn't crash
    assert(result)
end)

test("empty table", function()
    local t = {}
    assert(type(t) == "table")
    assert(#t == 0)
end)

test("nil in tables", function()
    local t = {1, nil, 3}
    assert(t[1] == 1)
    assert(t[2] == nil)
    assert(t[3] == 3)
end)

-- Final summary
print("\n=== Test Results ===")
print("Passed: " .. passed)
print("Failed: " .. failed)
print("Total:  " .. (passed + failed))

if failed == 0 then
    print("\n✓ All tests passed!")
else
    print("\n✗ Some tests failed")
end
