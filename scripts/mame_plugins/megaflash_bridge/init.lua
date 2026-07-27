-- license:BSD-3-Clause
-- Forward Apple //c memexp soft-switches ($C0C0-$C0C3) to Bramble's
-- MegaFlash a2bus TCP bridge. Keep MAME at default 128K so built-in Slinky
-- stays inert; taps override the floating-bus values with MegaFlash bytes.
-- Optionally overlay MegaFlash iic.bin onto :maincpu after reset (CRC-clean stock
-- dump stays on disk for MAME's ROM loader).

local exports = {
	name = "megaflash_bridge",
	version = "0.1.1",
	description = "Bramble MegaFlash $C0C0-$C0C3 TCP bridge",
	license = "BSD-3-Clause",
	author = { name = "eositis" }
}

local plugin = exports

local function getenv_port()
	local p = os.getenv("BRAMBLE_A2BUS_PORT")
	if p and tonumber(p) then
		return tonumber(p)
	end
	return 19765
end

local function load_iic_rom()
	local path = os.getenv("BRAMBLE_IIC_BIN")
	if not path or path == "" then
		return
	end

	local f = io.open(path, "rb")
	if not f then
		emu.print_error(string.format("megaflash_bridge: cannot open IIC_BIN %s", path))
		return
	end
	local data = f:read("*a")
	f:close()
	if not data or #data < 0x8000 then
		emu.print_error(string.format("megaflash_bridge: IIC_BIN too small (%s, %d bytes)",
			path, data and #data or 0))
		return
	end

	local region = manager.machine.memory.regions[":maincpu"]
	if not region then
		emu.print_error("megaflash_bridge: :maincpu region missing")
		return
	end

	for i = 0, 0x7fff do
		region:write_u8(i, data:byte(i + 1))
	end
	emu.print_info(string.format("megaflash_bridge: overlaid %s onto :maincpu (32768 bytes)", path))
end

local function open_bridge()
	local port = getenv_port()
	local sock = emu.file("", 7) -- OPEN_FLAG_READ | OPEN_FLAG_WRITE
	local path = string.format("socket.127.0.0.1:%d", port)
	local err = sock:open(path)
	if err then
		emu.print_error(string.format("megaflash_bridge: open %s failed (%s)", path, tostring(err)))
		return nil
	end
	emu.print_info(string.format("megaflash_bridge: connected to %s", path))
	return sock
end

local function rpc(sock, req)
	if not sock then
		return nil
	end
	sock:write(req)
	local deadline = os.clock() + 2.0
	local buf = ""
	while #buf < 2 do
		local chunk = sock:read(2 - #buf)
		if chunk and #chunk > 0 then
			buf = buf .. chunk
		elseif os.clock() > deadline then
			emu.print_error("megaflash_bridge: RPC timeout")
			return nil
		end
	end
	local status = buf:byte(1)
	local data = buf:byte(2)
	if status ~= 0 then
		emu.print_error(string.format("megaflash_bridge: RPC status=%d", status))
		return nil
	end
	return data
end

function plugin.startplugin()
	local sock = nil
	local taps = {}

	local function install_taps()
		local cpu = manager.machine.devices[":maincpu"]
		if not cpu then
			emu.print_error("megaflash_bridge: :maincpu missing")
			return
		end
		local space = cpu.spaces["program"]
		if not space then
			emu.print_error("megaflash_bridge: program space missing")
			return
		end

		load_iic_rom()

		if not sock then
			sock = open_bridge()
			if sock then
				local pong = rpc(sock, string.char(0x00))
				if pong then
					emu.print_info(string.format("megaflash_bridge: PING -> 0x%02X", pong))
				end
			end
		end

		taps.read = space:install_read_tap(0xc0c0, 0xc0c3, "megaflash_r",
			function(offset, data, mask)
				if not sock then
					return data
				end
				local nibble = offset & 0x3
				local v = rpc(sock, string.char(0x02, nibble))
				if v then
					return v
				end
				return data
			end)

		taps.write = space:install_write_tap(0xc0c0, 0xc0c3, "megaflash_w",
			function(offset, data, mask)
				if not sock then
					return
				end
				local nibble = offset & 0x3
				rpc(sock, string.char(0x03, nibble, data & 0xff))
			end)

		emu.print_info("megaflash_bridge: taps installed on $C0C0-$C0C3")
	end

	emu.add_machine_reset_notifier(function()
		install_taps()
	end)

	emu.add_machine_stop_notifier(function()
		taps = {}
		if sock then
			sock:close()
			sock = nil
		end
	end)
end

return exports
