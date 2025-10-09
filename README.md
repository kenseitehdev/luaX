# LuaX

LuaX is a lightweight Lua-inspired language and virtual machine designed for fast execution, coroutines, tables, first-class functions, multiple-return values, and structured error handling.

---

## Table of Contents

- [Introduction](#introduction)
- [Installation](#installation)
- [Language Basics](#language-basics)
  - [Variables and Types](#variables-and-types)
  - [Tables](#tables)
  - [Functions and Closures](#functions-and-closures)
  - [Multiple Returns](#multiple-returns)
- [Coroutines](#coroutines)
- [Error Handling](#error-handling)
- [Standard Libraries](#standard-libraries)
- [Example Script](#example-script)
- [Contributing](#contributing)

---

## Introduction

LuaX is designed for:

- Lightweight scripting.
- Fast execution with a fixed-size VM stack.
- Coroutines and multiple-return values.
- Tables and first-class functions.
- Structured error handling via `ErrFrame` (try/catch-like).
- Integration with C functions (`VAL_CFUNC`).

---

## Installation

Clone the repository:

```
git clone https://github.com/your-repo/luax.git
cd luax
make
```

Run scripts:

```
./bin/luaX your_script.lx
```

or

```
./bin/luaX your_script.lua
```

---

## Language Basics

### Variables and Types

```
local x = 123        -- integer
local y = 3.14       -- number
local b = true       -- boolean
local s = "hello"    -- string
local t = {}         -- table
local f = function(a) return a*2 end -- function

print(x, y, b, s, t, f)
```

**Expected Output:**

```
123 3.14 true hello table:0x7ff... function:0x7ff...
```

### Tables

```
local person = { name = "Alice", age = 30 }
person.city = "Wonderland"

for k, v in pairs(person) do
    print(k, v)
end
```

**Expected Output:**

```
name Alice
age 30
city Wonderland
```

### Functions and Closures

```
local function make_counter()
    local count = 0
    return function()
        count = count + 1
        return count
    end
end

local counter = make_counter()
print(counter()) -- 1
print(counter()) -- 2
```

### Multiple Returns

```
local function swap(a, b)
    return b, a
end

local x, y = swap(1, 2)
print(x, y)
```

**Expected Output:**

```
2 1
```

---

## Coroutines

```
local co = coroutine.create(function()
    for i=1,3 do
        print("co", i)
        coroutine.yield(i)
    end
end)

while coroutine.status(co) ~= "dead" do
    local ok, val = coroutine.resume(co)
    print("resumed:", val)
end
```

**Expected Output:**

```
co 1
resumed: 1
co 2
resumed: 2
co 3
resumed: 3
```

---

## Error Handling

```
local ok, err = pcall(function()
    error("Something went wrong!")
end)

if not ok then
    print("Caught error:", err)
end
```

**Expected Output:**

```
Caught error: Something went wrong!
```

---

## Standard Libraries

LuaX includes:

- `math` – standard math functions.
- `string` – string manipulation.
- `table` – table utilities.
- `io` – input/output.
- `os` – operating system.
- `coroutine` – coroutines.
- `random` – random numbers.
- `utf8` – UTF-8 utilities.
- `date` – date and time functions.
- `async` – async helpers.
- `class` – class/object system.
- `exception` – structured error handling.
- `package` – module system.

---

## Example Script

```
local function greet(name)
    return "Hello, " .. name
end

local names = {"Alice", "Bob", "Charlie"}
for _, n in ipairs(names) do
    print(greet(n))
end
```

**Expected Output:**

```
Hello, Alice
Hello, Bob
Hello, Charlie
```

---

## Contributing

1. Fork the repository.
2. Create a new branch: `git checkout -b feature/my-feature`.
3. Make your changes.
4. Commit your work: `git commit -am 'Add new feature'`.
5. Push to the branch: `git push origin feature/my-feature`.
6. Submit a pull request.
