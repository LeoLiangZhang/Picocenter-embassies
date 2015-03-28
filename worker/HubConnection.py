import zmq
import tornado.ioloop
import zmq.eventloop
from zmq.eventloop.zmqstream import ZMQStream
# from enum import Enum
import msgpack

class MessageType:
    PICO_RELEASE = 'R'
    PICO_KILL = 'K'
    UPDATE_STATUS = 'S'
    HELLO = 'H'
    HEARTBEAT = 'B'
    REPLY = 'Y'
    PICO_EXEC = 'E'
    DEAD = 'D'

class WorkerStatus:
    AVAILABLE = 1
    OVERLOADED = 2

class HubConnection(object):

    defaults = {
        'heartbeat_interval' : 1.0
    }

    def do_job(self, msg):
        """
        Parses args out of any incoming message, 
        then passes these to the the right worker function.
        """

        empty, address, empty, request = msg
        args = msgpack.unpackb(request)
        
        # Liang: I'm not sure if this is the right way to get the type of the request
        if request[0] == MessageType.E:
            result = self.worker.pico_exec(*args)
        else: # TODO: fill in more methods here ...
            raise AttributeError('Unknown command')

        server = address.split('-')[1].zfill(2)
        reply = MessageType.REPLY + server + msgpack.packb(result)
        self.job_stream.send_multipart([b"", address, b"", reply])

    def heartbeat(self):
        """
        Sends out a heartbeat to the hub. 
        Note: The function is called by the heartbeat_timer.
        """
        beat = MessageType.HEARTBEAT
        self.heart_stream.send(beat)


    def set_worker(self, worker):
        self.worker = worker

    def update_worker_status(self, status):
        """
        Changes worker's status in the hub database

        Parameters:
            status : WorkerStatus
        """

        self.worker_status = status
        msg = MessageType.UPDATE_STATUS + str(status)
        self.send_stream.send(msg)

    def pico_release(self, pico_id):
        # self.send_stream(...)
        pass

    def do_handshake(self):
        """
        Used on startup to inform hub of which ips this worker manages, and
        which ip/port combination (haddr) can be used to contact it
        """

        hello = {
            'status' : self.worker_status,
            'haddr' : self.heart_ip + ':' + self.heart_port,
            'ips' : self.public_ips
        }
        msg = MessageType.HELLO + msgpack.packb(hello)
        self.send_socket.send(msg)
        reply = self.send_socket.recv()
        return reply == "OK"

    def connect(self):
        """
        Creates and starts all necessary threads and sockets for heartbeating,
        job polling, and message passing.

        Excepts self to contain:
            - worker
            - heart_ip
            - hub_ip / hub_port

        Returns:
            [0] on success
            [-1] error (not connected to hub)
        """

    # set alarm 
    # need to look up public_ips
        endpoint = "tcp://" + self.hub_ip + ':' + self.hub_port

        self.send_socket = zmq.Context().socket(zmq.DEALER)
        self.send_socket.identity = self.heart_ip + '/send'
        self.send_socket.connect(endpoint)

        # we do handshake in synchronize 
        if not self.do_handshake(self.send_socket):
            return -1
        self.send_stream = ZMQStream(self.send_socket)

        self.job_socket = zmq.Context().socket(zmq.DEALER)
        self.job_socket.identity = self.heart_ip + '/jobs'
        self.job_socket.connect(endpoint)
        self.job_stream = ZMQStream(self.job_socket)
        self.job_stream.on_recv(self.do_job)

        self.heart_socket = zmq.Context().socket(zmq.DEALER)
        self.heart_socket.identity = self.heart_ip + '/heart'
        self.heart_socket.connect(endpoint)
        self.heart_stream = ZMQStream(self.heart_socket)


        self.heartbeat_timer = tornado.ioloop.PeriodicCallback(self.heartbeat,
            self.heartbeat_interval, self.loop)
        return 0

    def start(self):
        """
        Call this method when the worker is ready to receive jobs.
        """
        self.heartbeat_timer.start()
        self.process_next()

    def __init__(self, loop, worker, status=WorkerStatus.AVAILABLE, **kwargs):
        """
        Setup instance variables from config dictionary
        """

        # liang: I prefer writing down all available options, because in this 
        # dynamic style, reviewer has to dig into the code to figure out all 
        # configurable options.
        # TODO: revisit the design here
        for option in defaults:
            setattr(self, option, defaults[option])
        for option in kwargs:
            setattr(self, option, kwargs[option])

        self.worker_status = status

        self.loop = loop
        self.worker = worker
        self.send_socket = None
        self.send_stream = None
        self.job_socket = None
        self.job_stream = None
        self.heart_socket = None
        self.heart.stream = None

    # The following could be used in the future to easily create rpc functions on the fly
    # without defining them or their arguments beforehand

    # def rpc(self, function_name, *args):
    #     msg = msgpack.packb({'method':function_name,'args':args})
    #     self.rpc_socket.send(msg)

    # def __getattr__(self, name):
    #     _m = lambda args: rpc(self, name, args)
    #     setattr(cls, attr, classmethod(_m))
    #     return _m

    # def shutdown(self):
    #     # TODO maybe try the fake message trick to force recv to return
    #     # TODO set while conditions to false, which will close other sockets
    #     self.send_socket.close()
