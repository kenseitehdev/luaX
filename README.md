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

```bash
git clone https://github.com/your-repo/luax.git
cd luax
make```

Run scripts:
```luax 
luaX your_script.lx```
or 

```luax 
luaX your_script.lua```
##Language Basics
###Variables and Types 
```luax
local x = 123        -- integer
local y = 3.14       -- number
local b = true       -- boolean
local s = "hello"    -- string
local t = {}         -- table
local f = function(a) return a*2 end -- function

print(x, y, b, s, t, f)
```

####Expected Output
```luaX
123 3.14 true hello table:0x7ff... function:0x7ff...
```
