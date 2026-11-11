-- Flycast 2.7 SH4 Diagnostic Test
-- Automated test for task_counter and failures, with framebuffer capture.

local vblank_count = 0
local capture_done = false
local result_written = false
local error_msg = nil

local function write_json(path, data)
  local f = io.open(path, "w")
  if not f then
    error_msg = "Failed to open JSON file: " .. path
    return false
  end
  f:write('{"counter":' .. data.counter .. ',"failures":' .. data.failures .. ',"pass":' .. (data.pass and 'true' or 'false') .. '}')
  f:close()
  return true
end

local function write_ppm(path, base_addr)
  local f = io.open(path, "wb")
  if not f then
    error_msg = "Failed to open PPM file: " .. path
    return false
  end
  f:write("P6\n640 480\n255\n")
  for y = 0, 479 do
    local row = {}
    local pixels = flycast.memory.readTable16(base_addr + y * 640 * 2, 640)
    for i = 1, 640 do
      local v = pixels[base_addr + y * 640 * 2 + (i - 1) * 2]
      local r = math.floor((math.floor(v / 2048) % 32) * 255 / 31)
      local g = math.floor((math.floor(v / 32) % 64) * 255 / 63)
      local b = math.floor((v % 32) * 255 / 31)
      row[#row + 1] = string.char(r, g, b)
    end
    f:write(table.concat(row))
  end
  f:close()
  return true
end

flycast_callbacks = {
  start = function()
    vblank_count = 0
    capture_done = false
    result_written = false
    error_msg = nil
  end,
  vblank = function()
    vblank_count = vblank_count + 1
  end,
  overlay = function()
    if vblank_count >= 600 and not capture_done then
      capture_done = true
      local ok, err = pcall(function()
        local counter = flycast.memory.read32(0x8c06d67c)
        local failures = flycast.memory.read32(0x8c06ecbc)
        local pass = (counter == 1000 and failures == 0)
        
        local json_ok = write_json("D:/OutRunners-Dreamcast/logs/diagnostic-native-result.json", {counter=counter, failures=failures, pass=pass})
        if not json_ok then return end
        
        local sof1 = flycast.memory.read32(0xa05f8050) % 0x800000
        local base = 0xa5000000 + sof1
        local ppm_ok = write_ppm("D:/OutRunners-Dreamcast/emulator/screenshots/diagnostic-native.ppm", base)
        if not ppm_ok then return end
        
        result_written = true
      end)
      if not ok then
        error_msg = err
      end
    end
    
    if (capture_done and result_written) or error_msg then
      if error_msg then
        print("ERROR: " .. error_msg)
        local ef = io.open("D:/OutRunners-Dreamcast/logs/diagnostic-error.txt", "w")
        if ef then
          ef:write(error_msg)
          ef:close()
        end
      end
      flycast.emulator.exit()
    end
  end
}
