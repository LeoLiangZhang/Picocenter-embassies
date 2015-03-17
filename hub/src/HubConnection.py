import zmq
from threading import Thread
from time import sleep
from enum import Enum
import msgpack

class MessageType(Enum):
    PICO_RELEASE = 'R'
    PICO_KILL = 'K'
    UPDATE_STATUS = 'S'

class WorkerStatus(Enum):
    AVAILABLE = 1
    OVERLOADED = 2

class HubConnection(object):

    defaults = {
        'heartbeat_interval' : 1.0
    }

    def wait_for_jobs(identity, endpoint, callback):
        """
        Waits for pico_exec messages from the hub, parses args out of any incoming
        message, then passes these to the callback function (worker.pico_exec)

        Parameters:
            identity : string, unique identifier of this thread's socket
            endpoint : string (transport,ip,port)
            callback : function (pico_id : string, private_ip : string, port_map : dictionary<string,list<int>>, flags : int) -> Boolean
        """

        job_socket = zmq.Context().socket(zmq.DEALER)
        job_socket.identity = identity
        job_socket.connect(endpoint)

        while True:
            empty, address, empty, request = job_socket.recv_multipart()
            args = msgpack.unpackb(request)
            result = callback(args)
            reply = msgpack.packb(result)
            job_socket.send_multipart([b"", address, b"", reply])
        job_socket.close()

    def heartbeat(identity, endpoint):
        """
        Sends out a heartbeat to the hub every HEARTBEAT_INTERVAL seconds.
        """

        heart_socket = zmq.Context().socket(zmq.DEALER)
        heart_socket.identity = identity
        heart_socket.connect(endpoint)

        beat = msgpack.packb(1)

        while True:
            heart_socket.send(beat)
            sleep(self.heartbeat_interval)
        heart_socket.close()


    def set_worker(self, worker):
        self.worker = worker

    def pico_release(self, pico_id):
        """
        Called when picoprocess is swapped (hot/warm -> cold), remains in database

        Parameters
            pico_id : string
        """

        msg = MessageType.PICO_RELEASE + pico_id
        self.send_socket.send(msg)

    def pico_kill(self, pico_id):
        """
        Called when picoprocess exits, removes picoprocess from hub database

        Paramaeters:
            pico_id : string
        """

        msg = MessageType.PICO_KILL + pico_id
        self.send_socket.send(msg)

    def update_worker_status(self, status):
        """
        Changes worker's status in the hub database

        Parameters:
            status : WorkerStatus
        """

        self.worker_status = status
        msg = MessageType.UPDATE_STATUS + str(status)
        self.send_socket.send(msg)

    def do_handshake(self, socket):
        """
        Used on startup to inform hub of which ips this worker manages, and
        which ip/port combination (haddr) can be used to contact it
        """

        hello = {
            'status' : self.worker_status,
            'haddr' : self.heart_ip + ':' + self.heart_port,
            'ips' : self.public_ips
        }
        msg = msgpack.packb(hello)
        socket.send(msg)
        reply = socket.recv()
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

        endpoint = "tcp://" + self.hub_ip + ':' + self.hub_port

        self.send_socket = zmq.Context().socket(zmq.DEALER)
        self.send_socket.identity = self.heart_ip + '/send'
        self.send_socket.connect(endpoint)

        if not self.do_handshake(send_socket):
            return -1

        job_poll = Thread(target = wait_for_jobs, args = (self.heart_ip + '/jobs', endpoint, self.worker.pico_exec, ))
        job_poll.daemon = True
        job_poll.start()

        heart = Thread(target = heartbeat args = (self.heart_ip + '/heart', endpoint, ))
        heart.daemon = True
        heart.start()

        return 0

    def __init__(self, config, status=WorkerStatus.AVAILABLE):
        """
        Setup instance variables from config dictionary
        """

        for option in defaults:
            setattr(self, option, defaults[option])

        for option in config:
            setattr(self, option, config[option])

        self.worker_status = status

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