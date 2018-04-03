
local asio = require 'asio'

-- test create and remove thread
do io.write('---- Light Thread Test ----')

    -- create 3 thread
    assert(asio._get_free_tid() == 1)
    local tid, useid = asio._get_free_tid()
    asio._use_tid(tid, useid, 't1')

    assert(asio._get_free_tid() == 2)
    local tid, useid = asio._get_free_tid()
    asio._use_tid(tid, useid, 't2')

    assert(asio._get_free_tid() == 3)
    local tid, useid = asio._get_free_tid()
    asio._use_tid(tid, useid, 't3')

    assert(asio._get_free_tid() == 4)
    assert(asio._get_tid('t2') == 2)

    --remove 1
    asio._remove_th(2)
    assert(asio._get_free_tid() == 2)

    --recreate thread
    local tid, useid = asio._get_free_tid()
    asio._use_tid(tid, useid, 'r2')

    assert(asio._get_free_tid() == 4)
    assert(asio._get_tid('t2') == nil)
    assert(asio._get_tid('r2') == 2)

    --clean
    asio._remove_th(3)
    asio._remove_th(2)
    asio._remove_th(1)
    assert(asio._get_free_tid() == 1)

    --spaw thread
    function th_test(arg1, tid)
        assert(arg1 == 'tttt')
        assert(asio._get_tid(coroutine.running()) == tid)
        coroutine.yield()
    end

    local th1 = asio.spawn_light_thread(th_test, 'tttt', 1)
    local th2 = asio.spawn_light_thread(th_test, 'tttt', 2)
    local th3 = asio.spawn_light_thread(th_test, 'tttt', 3)

    coroutine.resume(th2)
    assert(asio._get_free_tid() == 2)

    coroutine.resume(th1)
    assert(asio._get_free_tid() == 1)

    coroutine.resume(th3)
    assert(asio._get_free_tid() == 3)

end io.write(' \t[OK]\n')

--------------------------------------------------
local ffi = require("ffi")
ffi.cdef[[
void Sleep(int ms);
int poll(struct pollfd *fds, unsigned long nfds, int timeout);
]]

local sleep
if ffi.os == "Windows" then
  function sleep(s)
    ffi.C.Sleep(s*1000)
  end
else
  function sleep(s)
    ffi.C.poll(nil, 0, s*1000)
  end
end
--------------------------------------------------

--test network
--for i=1,1000
do io.write('---- C Asio Test ----')

    -- non-ip address should return nil
    assert( not asio.server('localhost', 1234) )
    -- not in light thread
    assert( pcall( asio.connect, 'localhost', 1234) == false )
    -- connect faile
    local con = 'not set con'
    asio.spawn_light_thread(function()
        con = asio.connect('0.0.0.1', '1234')
    end)
    asio.run()
    assert(con == nil, con)

    -- server
    function connection_th(con)
        --print('server')
        local data, err = con:read(5)
        --print('server', data, 'readed', e)
        assert(data == 'ping1' or data == 'ping2' or data == 'ping3')
        con:write(data .. '-pong')
        con:close()
    end

    local s = asio.server('127.0.0.1', 31234, function(con)
        asio.spawn_light_thread(connection_th, con)
    end)
    local total =0
    local ping_send = function(text)
        --print('client', text)
        local con, e = asio.connect('localhost', '31234')
        assert(con, e)
        --print('client', text, 'conned', e)
        local ok, e = con:write(text)
        local data, err = con:read_some()
        assert(data == 'ping1-pong' or data == 'ping2-pong' or data == 'ping3-pong')
        con:close()
        total = total + 1
        if total ==3 then
            asio.destory_server(s)
            s = nil
        end
    end
    asio.spawn_light_thread(ping_send, 'ping1')
    asio.spawn_light_thread(ping_send, 'ping2')
    asio.spawn_light_thread(ping_send, 'ping3')
    asio.run()

end io.write(' \t\t[OK]\n')

--bench
do io.write('---- C Asio Bench ----')

    --benchmark
    local connects = 10
    local sends = 1000
    local function client_bench()
        local con, e = asio.connect('localhost', '31234')
        for i=1,sends do
            con:write('123456789|123456789|123456789|123456789|123456789|')
            con:read(50)
        end
        con:close()
    end
    local total = 0
    local s
    local function server_bench(con)
        for i=1,sends do
            con:read(50)
            con:write('123456789|123456789|123456789|123456789|123456789|')
            total = total + 1
        end
        con:close()
        if total == sends*connects then
            asio.destory_server(s)
        end
    end
    s = asio.server('127.0.0.1', 31234, function(con)
        asio.spawn_light_thread(server_bench, con)
    end)
    for i = 1,connects do
        asio.spawn_light_thread(client_bench)
    end
    asio.run()

end io.write(' \t\t[OK]\n')

print('All Tests passed.')