import zmq
import sys
from HubConnection import MessageType, WorkerStatus
from PicoManager import PicoManager
from random import choice
from time import sleep
from threading import Thread
from multiprocessing import Process
import msgpack
import MySQLdb

PICOPROCESS_TABLE = 'picos'
WORKER_TABLE = 'workers'
ADDR_TABLE = 'addrmap'
META_TABLE = 'meta'

import config
logger = config.logger

################################################################################

def db_manager_process():

    db = MySQLdb.connect(host='localhost',user='root',passwd='pewee2brandy',db='picocenter')
    db.autocommit(True)
    cursor = db.cursor()

    context = zmq.Context.instance()
    router = context.socket(zmq.ROUTER)
    router.bind("ipc://db.ipc")

    picomanager = PicoManager(db, router)

    while True:
        msg = router.recv()

        mtype = msg[0]
        if mtype == MessageType.PICO_RELEASE:
            pico_id = msg[1:]
            query = ("UPDATE {0} SET hot=FALSE, worker_id=NULL, public_ip=NULL WHERE pico_id='{1}' ".format(PICOPROCESS_TABLE, pico_id))
        elif mtype == MessageType.PICO_KILL:
            pico_id = msg[1:]
            query = ("DELETE FROM {0} WHERE pico_id='{1}'".format(PICOPROCESS_TABLE, pico_id))
        elif mtype == MessageType.UPDATE_STATUS:
            status = msgpack.unpackb(msg[1])
            if status:
                status = 'available'
            else:
                status = 'overloaded'
            worker_ip = msg[2:]
            query = ("UPDATE {0} SET status='{1}' WHERE heart_ip='{2}'".format(WORKER_TABLE, status, worker_ip))
        elif mtype == MessageType.DEAD:
            worker_ip = msg[1:]
            picomanager.migrate_picoprocesses(worker_ip)

    cursor.close()

################################################################################

heartbeats = {}
heartbeat_timeout = 5.0

def monitor_heartbeats():
    monitor_socket = zmq.Context().socket(zmq.DEALER)
    monitor_socket.connect('ipc://db.ipc')
    while True:
        logger.debug("Heartbeat Monitor: (workers={0})".format(str(heartbeats)))
        for worker in heartbeats:
            if heartbeats[worker] == 0:
                monitor_socket.send(MessageType.DEAD + worker)
                del heartbeates[worker]
            else:
                heartbeats[worker] = 0
        sleep(heartbeat_timeout)

################################################################################

args = ['port']
if len(sys.argv) < len(args)+1:
    sys.exit('usage: python hub-heart.py ' + ' '.join(['['+arg+']' for arg in args]))

db_manager = Process(target=db_manager_process)
db_manager.daemon = True
db_manager.start()

monitor = Thread(target=monitor_heartbeats)
monitor.daemon = True
monitor.start()

################################################################################

context = zmq.Context.instance()
frontend = context.socket(zmq.ROUTER)
frontend.bind("ipc://frontend.ipc")
backend = context.socket(zmq.ROUTER)
backend.bind("tcp://0.0.0.0:" + sys.argv[1])
dispatcher = context.socket(zmq.DEALER)
dispatcher.connect('ipc://db.ipc')

poller = zmq.Poller()
poller.register(backend, zmq.POLLIN)
workers = False

while True:
    sockets = dict(poller.poll())

    ###########################################################################

    if backend in sockets:

        request = backend.recv_multipart()

        worker, msg = request[:2]
        mtype = msg[0]

        if mtype == MessageType.HELLO:
            if not workers:
                poller.register(frontend, zmq.POLLIN)
            worker_ip = worker.split('/')[0]
            workers = True
            heartbeats[worker_ip] = 0
            logger.debug("Found new worker: {0}".format(worker_ip))
        elif mtype == MessageType.HEARTBEAT:
            worker_ip = worker.split('/')[0]
            heartbeats[worker_ip] = 1
            logger.debug("Heartbeat recieved from {0}".format(worker_ip))
        elif mtype == MessageType.PICO_RELEASE or mtype == MessageType.PICO_KILL:
            dispatcher.send(msg)
        elif mtype == MessageType.UPDATE_STATUS:
            worker_ip = worker.split('/')[0]
            dispatcher.send(msg + worker_ip)
        elif mtype == MessageType.REPLY:
            dest = request[2]
            logger.debug("Forwarding reply to {0}".format(dest))
            frontend.send_multipart(dest, b"", msg[1:])
        else:
            logger.critical("Unknown message type:" + str(mtype))

    ###########################################################################

    if frontend in sockets:

        client, msg = frontend.recv_multipart()
        logger.debug("From DNS: client={0}, msg={1}".format(str(client), str(msg)))
        worker, request = msg.split('|')
        worker = worker + "/jobs"
        backend.send_multipart([worker, "", client, "", request])
        if not workers:
            poller.unregister(frontend)

    ###########################################################################

db_manager.terminate()
backend.close()
frontend.close()
context.term()
