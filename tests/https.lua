assert(http.tls_available(), "build this test with LUAC_WITH_MBEDTLS")
local port = os.getenv("LUAC_HTTPS_TEST_PORT")
local ca = os.getenv("LUAC_HTTPS_TEST_CA")
assert(port and ca, "HTTPS test server configuration is missing")
local url = "https://localhost:" .. port .. "/"

local rejected, rejection = http.get(url)
assert(rejected == nil, "a self-signed certificate was trusted")
assert(rejection and (#rejection > 0))

assert(http.setca(ca))
local body, status, headers = http.get(url)
assert(body == "LuaC mbedTLS test\n", tostring(body) .. ": " .. tostring(status))
assert(status == 200)
assert(headers["content-type"] == "text/plain")

-- The CA is trusted now, but its SAN is localhost rather than 127.0.0.1.
local wrongHost, hostError = http.get("https://127.0.0.1:" .. port .. "/")
assert(wrongHost == nil)
assert(hostError and (#hostError > 0))
print("LuaC HTTPS certificate and hostname verification: OK")
