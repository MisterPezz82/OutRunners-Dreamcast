-- Flycast 2.7 SH4 Prototype Capture
-- Automated capture for prototype measurements, keeps game running.

local vblank_count = 0
local capture_done = false
local result_written = false
local late_done = false
local next_shot = 3600
local error_msg = nil

local function write_json(path, data)
  local f = io.open(path, "w")
  if not f then
    error_msg = "Failed to open JSON file: " .. path
    return false
  end
  f:write('{"frames":' .. data.frames .. ',"io_failed":' .. data.io_failed .. ',"vblanks":' .. data.vblanks .. ',"boot_stage":' .. data.boot_stage .. ',"fault_code":' .. data.fault_code .. ',"fault_pc":' .. data.fault_pc .. ',"fault_pr":' .. data.fault_pr .. ',"fault_sp":' .. data.fault_sp .. ',"exit_status":' .. data.exit_status .. ',"exit_caller":' .. data.exit_caller .. '}')
  f:close()
  return true
end

local function write_ppm(path, base_addr)
  -- the frame buffer's size from FB_R_SIZE (16-bit pixels: (words - 1) in bits 0-9, lines - 1 in 10-19)
  local size = flycast.memory.read32(0xa05f805c)
  local width = ((size % 1024) + 1) * 2
  local height = (math.floor(size / 1024) % 1024) + 1
  if width < 64 or width > 1024 then width = 640 end
  if height < 64 or height > 1024 then height = 480 end
  local f = io.open(path, "wb")
  if not f then
    error_msg = "Failed to open PPM file: " .. path
    return false
  end
  f:write("P6\n" .. width .. " " .. height .. "\n255\n")
  for y = 0, height - 1 do
    local row = {}
    local pixels = flycast.memory.readTable16(base_addr + y * width * 2, width)
    for i = 1, width do
      local v = pixels[base_addr + y * width * 2 + (i - 1) * 2]
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

-- Write start marker on script load
local marker_f = io.open("D:/OutRunners-Dreamcast/logs/prototype-lua-started.txt", "w")
if marker_f then
  marker_f:write("started\n")
  marker_f:close()
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
    -- the start-up screen, a few seconds in (before the game's first frame)
    -- the loading screen: a picture every 24 refreshes from 5 to 16 seconds in
    if vblank_count >= 300 and vblank_count <= 960 and vblank_count % 24 == 0 then
      pcall(function()
        local sof1 = flycast.memory.read32(0xa05f8050) % 0x800000
        write_ppm("D:/OutRunners-Dreamcast/emulator/screenshots/prototype-load-" .. math.floor((vblank_count - 300) / 24) .. ".ppm", 0xa5000000 + sof1)
      end)
    end
    if vblank_count == 180 then
      pcall(function()
        local sof1 = flycast.memory.read32(0xa05f8050) % 0x800000
        write_ppm("D:/OutRunners-Dreamcast/emulator/screenshots/prototype-startup.ppm", 0xa5000000 + sof1)
      end)
    end
    if vblank_count % 30 == 0 then
      local boot_stage = flycast.memory.read32(0x00000000)
      local frames = flycast.memory.read32(0x00000000)
      local fault_code = flycast.memory.read32(0x00000000)
      local fault_pc = flycast.memory.read32(0x00000000)
      local exit_status = flycast.memory.read32(0x00000000)
      local exit_caller = flycast.memory.read32(0x00000000)
      local stack_fault = flycast.memory.read32(0x00000000)
      local stack_free = flycast.memory.read32(0x00000000)
      local stack_free2 = flycast.memory.read32(0x00000000)
      local miss_tile = flycast.memory.read32(0x00000000)
      local miss_sprite = flycast.memory.read32(0x00000000)
      local miss_pcm = flycast.memory.read32(0x00000000)
      local heap_largest = flycast.memory.read32(0x00000000)
      local time_frame = flycast.memory.read32(0x00000000)
      local time_video = flycast.memory.read32(0x00000000)
      local time_sprites = flycast.memory.read32(0x00000000)
      local time_sound = flycast.memory.read32(0x00000000)
      local time_present = flycast.memory.read32(0x00000000)
      local time_passes = flycast.memory.read32(0x00000000)
      local time_pvrwait = flycast.memory.read32(0x00000000)
      local time_pvrwork = flycast.memory.read32(0x00000000)
      local pvr_0 = flycast.memory.read32(0x00000000)
      local pvr_1 = flycast.memory.read32(0x00000000)
      local pvr_2 = flycast.memory.read32(0x00000000)
      local pvr_3 = flycast.memory.read32(0x00000000)
      local pvr_4 = flycast.memory.read32(0x00000000)
      local pvr_5 = flycast.memory.read32(0x00000000)
      local pvr_6 = flycast.memory.read32(0x00000000)
      local pvr_7 = flycast.memory.read32(0x00000000)
      local pvr_8 = flycast.memory.read32(0x00000000)
      local pvr_9 = flycast.memory.read32(0x00000000)
      local pvr_10 = flycast.memory.read32(0x00000000)
      local pvr_11 = flycast.memory.read32(0x00000000)
      local pvr_12 = flycast.memory.read32(0x00000000)
      local pvr_13 = flycast.memory.read32(0x00000000)
      local pvr_14 = flycast.memory.read32(0x00000000)
      local pvr_15 = flycast.memory.read32(0x00000000)
      local aica_0 = flycast.memory.read32(0x00000000)
      local aica_1 = flycast.memory.read32(0x00000000)
      local aica_2 = flycast.memory.read32(0x00000000)
      local aica_3 = flycast.memory.read32(0x00000000)
      local aica_4 = flycast.memory.read32(0x00000000)
      local aica_5 = flycast.memory.read32(0x00000000)
      local aica_6 = flycast.memory.read32(0x00000000)
      local aica_7 = flycast.memory.read32(0x00000000)
      local f = io.open("D:/OutRunners-Dreamcast/logs/prototype-boot-trace.csv", "a")
      if f then
        f:write(table.concat({vblank_count, boot_stage, frames, fault_code, fault_pc, exit_status, exit_caller, stack_fault, stack_free, stack_free2, miss_tile, miss_sprite, miss_pcm, heap_largest, time_frame, time_video, time_sprites, time_sound, time_present, time_passes, time_pvrwait, time_pvrwork, pvr_0, pvr_1, pvr_2, pvr_3, pvr_4, pvr_5, pvr_6, pvr_7, pvr_8, pvr_9, pvr_10, pvr_11, pvr_12, pvr_13, pvr_14, pvr_15, aica_0, aica_1, aica_2, aica_3, aica_4, aica_5, aica_6, aica_7}, ",") .. "\n")
        f:close()
      end
    end
  end,
  overlay = function()
    if vblank_count >= 300 and not capture_done then
      local fault_code = flycast.memory.read32(0x00000000)
      local frames = flycast.memory.read32(0x00000000)
      if fault_code == 0 and frames < 1 then return end
      capture_done = true
      local ok, err = pcall(function()
        local frames = flycast.memory.read32(0x8c543398)
        local io_failed = flycast.memory.read32(0x8c4f08e8)
        local vblanks = vblank_count
        local boot_stage = flycast.memory.read32(0x00000000)
        local fault_code = flycast.memory.read32(0x00000000)
        local fault_pc = flycast.memory.read32(0x00000000)
        local fault_pr = flycast.memory.read32(0x00000000)
        local fault_sp = flycast.memory.read32(0x00000000)
        local exit_status = flycast.memory.read32(0x00000000)
        local exit_caller = flycast.memory.read32(0x00000000)

        local json_ok = write_json("D:/OutRunners-Dreamcast/logs/prototype-native-result.json", {frames=frames, io_failed=io_failed, vblanks=vblanks, boot_stage=boot_stage, fault_code=fault_code, fault_pc=fault_pc, fault_pr=fault_pr, fault_sp=fault_sp, exit_status=exit_status, exit_caller=exit_caller})
        if not json_ok then return end

        local sof1 = flycast.memory.read32(0xa05f8050) % 0x800000
        local base = 0xa5000000 + sof1
        local ppm_ok = write_ppm("D:/OutRunners-Dreamcast/emulator/screenshots/prototype-native.ppm", base)
        if not ppm_ok then return end

        result_written = true
      end)
      if not ok then
        error_msg = err
      end
    end

    -- (the console menus' checks: a picture every 300 vblanks too, s5 from vblank 1500)
    if capture_done and vblank_count >= 1500 and vblank_count <= 12000 and vblank_count % 300 == 0 then
      pcall(function()
        local sof1 = flycast.memory.read32(0xa05f8050) % 0x800000
        write_ppm("D:/OutRunners-Dreamcast/emulator/screenshots/prototype-native-s" .. math.floor(vblank_count / 300) .. ".ppm", 0xa5000000 + sof1)
      end)
    end
    if capture_done and vblank_count >= next_shot then
      local n = math.floor(next_shot / 1800)
      next_shot = next_shot + 1800
      pcall(function()
        local sof1 = flycast.memory.read32(0xa05f8050) % 0x800000
        write_ppm("D:/OutRunners-Dreamcast/emulator/screenshots/prototype-native-t" .. n .. ".ppm", 0xa5000000 + sof1)
      end)
    end
    if capture_done and not late_done and vblank_count >= 1800 then
      late_done = true
      pcall(function()
        local sof1 = flycast.memory.read32(0xa05f8050) % 0x800000
        write_ppm("D:/OutRunners-Dreamcast/emulator/screenshots/prototype-native-late.ppm", 0xa5000000 + sof1)
      end)
    end

    if error_msg then
      print("ERROR: " .. error_msg)
      local ef = io.open("D:/OutRunners-Dreamcast/logs/prototype-error.txt", "w")
      if ef then
        ef:write(error_msg)
        ef:close()
      end
    end
    -- No emulator.exit call; keep process running
  end
}
