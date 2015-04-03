import zmq
import tornado.ioloop
import zmq.eventloop
from zmq.eventloop.zmqstream import ZMQStream
import msgpack

import config
logger = config.logger

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
        'heartbeat_interval' : 3000.0
    }

    def do_job(self, msg):
        """
        Parses args out of any incoming message,
        then passes these to the the right worker function.
        """
	logger.debug("Recieved job request")
        empty, address, empty, request = msg
        logger.debug("Request: " + str(request))
        args = msgpack.unpackb(request)
        logger.debug("Args: " + str(args))

        # Liang: I'm not sure if this is the right way to get the type of the request
        #if request[0] == MessageType.PICO_EXEC
        #    result = self.worker.pico_exec(*args)
	#elif request[0] == MessageType.PICO_RELEASE:
	#    result = self.worker.pico_release(*args)
        #else: # TODO: fill in more methods here ...
        #    raise AttributeError('Unknown command')

        self.worker.pico_exec(5, '10.2.0.5', '192.168.1.50:4040.TCP=10.2.0.5:8080', False)
	ret = 0
        reply = MessageType.REPLY + str(ret)
        logger.debug("Reply: " + str(reply))
        self.job_stream.send_multipart([reply, address])

    def heartbeat(self):
        """
        Sends out a heartbeat to the hub.
        Note: The function is called by the heartbeat_timer.
        """
	logger.debug("Sending heartbeat...")
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
	logger.debug("Update worker status from {0} to {1}".format(self.worker_status, status))
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
	# assert send socket already connected
        hello = {
            'status' : self.worker_status,
            'haddr' : self.heart_ip + ':' + self.heart_port,
            'ips' : self.public_ips
        }
        msg = MessageType.HELLO + msgpack.packb(hello)
	logger.debug("Initiating handshake...")
        self.send_socket.send(msg)
	logger.debug("SENT")
        #reply = self.send_socket.recv()
	#logger.debug("Recieved reply " + str(reply))
        #return reply == "OK"
	return True

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
	logger.debug("Hub connect")
        endpoint = "tcp://" + self.hub_ip + ':' + self.hub_port

	context = zmq.Context.instance()

        self.send_socket = context.socket(zmq.DEALER)
        self.send_socket.identity = self.heart_ip + '/send'
        self.send_socket.connect(endpoint)
	logger.debug("Send socket connected")

        self.job_socket = context.socket(zmq.DEALER)
        self.job_socket.identity = self.heart_ip + '/jobs'
        self.job_socket.connect(endpoint)
        self.job_stream = ZMQStream(self.job_socket)
        self.job_stream.on_recv(self.do_job)
	logger.debug("Job socket connected")

        self.heart_socket = context.socket(zmq.DEALER)
        self.heart_socket.identity = self.heart_ip + '/heart'
        self.heart_socket.connect(endpoint)
        self.heart_stream = ZMQStream(self.heart_socket)
	logger.debug("Heartbeat socket connected")


        self.heartbeat_timer = tornado.ioloop.PeriodicCallback(self.heartbeat,
            self.heartbeat_interval, self.loop)

        return 0

    def start(self):
        """
        Call this method when the worker is ready to receive jobs.
        """
	logger.debug("Hub start")
        # we do handshake in synchronize
        if not self.do_handshake():
	    logger.debug("\tHandshake failed...")
            return -1
	logger.debug("Handshake successful!")
        self.send_stream = ZMQStream(self.send_socket)
        self.heartbeat_timer.start()
	self.job_stream.on_recv(self.do_job)

    def __init__(self, loop, worker, status=WorkerStatus.AVAILABLE, **kwargs):
        """
        Setup instance variables from config dictionary
        """
	logger.debug("Initiating hub connection instance...")
        # liang: I prefer writing down all available options, because in this
        # dynamic style, reviewer has to dig into the code to figure out all
        # configurable options.
        # TODO: revisit the design here
        for option in self.defaults:
            setattr(self, option, self.defaults[option])
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
        self.heart_stream = None
	# TEMP
	self.hub_ip = '0.0.0.0'
	self.hub_port = '9997'
	self.heart_ip = '1.2.3.4'
	self.heart_port = '5678'
	self.public_ips = ['1.2.3.4','1.2.3.5']

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
