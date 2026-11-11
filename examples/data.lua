local record = {
  name = "LuaC",
  native = true,
  libraries = {"json", "socket", "engine"}
}
local encoded = json.encode(record)
print(encoded)
assert(hash.sha256(encoded) == hash.sha256(json.encode(json.decode(encoded))))
print("Base64: " .. base64.encode(encoded))
print("URL: " .. url.encode("native data/01"))
print("SHA-256: " .. hash.sha256(encoded))
