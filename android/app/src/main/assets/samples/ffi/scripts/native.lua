local native = {}
function native.run()
  local library, err = ffi.open('libc.so.6')
  if not library then library, err = ffi.open('libc.so') end
  if not library then return 'Native library unavailable: ' .. tostring(err) end
  local result = ffi.call(library, 'abs', 'int32(int32)', -42)
  ffi.close(library)
  return 'C abs(-42) = ' .. tostring(result)
end
return native
