

function update_configure(c)

	local x = c:add_maps()
	
	local y = x:mutable_local()

	y:set_addr("0.0.0.0")
	y:set_port(7777)

	local z = x:add_remotes()

	z:set_addr("192.168.2.193")
	z:set_port(8088)


	z = x:add_remotes()
	z:set_addr("192.168.2.230")
	z:set_port(8088)

    c:set_run_as_daemon(0)                                                          --服务器后台运行 using both
end
