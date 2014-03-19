

function update_configure(c)

	local x = c:add_maps()
	
	local l
	l = x:add_locals()
	l:set_addr("0.0.0.0")
	l:set_port(9999)
	
	l = x:add_locals()
	l:set_addr("0.0.0.0")
	l:set_port(8888)
	
	local r
	r = x:add_remotes()

	r:set_addr("192.168.2.193")
	r:set_port(8188)


	r = x:add_remotes()
	r:set_addr("192.168.2.230")
	r:set_port(8088)

    c:set_run_as_daemon(0)                                                          --服务器后台运行 using both
end
