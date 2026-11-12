-- Build with mbedTLS (see README). Certificate and hostname verification are
-- enabled by default; plaintext fallback is never attempted.
assert(http.tls_available(), "rebuild LuaC with LUAC_WITH_MBEDTLS")
local body, status, headers = http.get("https://github.com/")
assert(body, tostring(status))
print("HTTPS status:", status)
print("Response bytes:", #body)
print("Content-Type:", headers["content-type"])
