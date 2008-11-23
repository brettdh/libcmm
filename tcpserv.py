#!/usr/bin/env python

# a simple tcp server

import SocketServer

class EchoRequestHandler(SocketServer.BaseRequestHandler ):
    def setup(self):
        print self.client_address, 'connected!'
        #self.request.send('hi ' + str(self.client_address) + '\n')

    def handle(self):
        data = 'dummy'
        while data:
            data = self.request.recv(1024)
            #self.request.send(data)
	    print "%s %s" % (str(self.client_address), data)
            if data.strip() == 'bye':
                return

    def finish(self):
        print self.client_address, 'disconnected!'
        #self.request.send('bye ' + str(self.client_address) + '\n')

    #server host is a tuple ('host', port)

class MySockServer(SocketServer.ThreadingTCPServer):
    def __init__(self, addr, handler):
        self.allow_reuse_address = True
        SocketServer.ThreadingTCPServer.__init__(self, addr, handler)

#server = SocketServer.ThreadingTCPServer(('', 4242), EchoRequestHandler)
server = MySockServer(('', 4242), EchoRequestHandler)
server.allow_reuse_address = True
server.serve_forever()
