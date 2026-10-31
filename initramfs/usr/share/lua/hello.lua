print(_VERSION .. " on ProotOS")
local function fib(n)
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end
local t = {}
for i = 1, 12 do t[i] = fib(i) end
print("fib: " .. table.concat(t, " "))
local sum = 0
for i = 1, 100 do sum = sum + i end
print("sum 1..100 = " .. sum)
print(string.format("pi ~ %.5f, sqrt(2) ~ %.5f", math.pi, math.sqrt(2)))
print(("prootos"):upper(), #("hello world"))
local words = {}
for w in ("one two three four"):gmatch("%a+") do words[#words + 1] = w end
print("words:", #words, table.concat(words, ","))
local ok, err = pcall(function() error("caught by pcall") end)
print("pcall:", ok, err)
