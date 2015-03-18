from twisted.internet import reactor, defer
from twisted.names import client, dns, error, server
import MySQLdb
import sys, os
import random
import zmq
import msgpack

################################################################################

RECORD_TTL = 0
PICOPROCESS_TABLE = 'picos'
WORKER_TABLE = 'workers'
ADDR_TABLE = 'addrmap'
META_TABLE = 'meta'

PICO_FIELDS = "pico_id,hot,worker_id,public_ip,internal_ip,ports,hostname,customer_id"
WORKER_FIELDS = "worker_id,status,heart_ip,heart_port"
ADDR_FIELDS = "worker_id,public_ip,ports_allocated,port_80_allocated"

################################################################################

class Picoprocess(object):
    def __init__(self, fields):
        self.pico_id, self.hot, self.worker_id, self.public_ip, self.internal_ip, self.ports, self.hostname, self.customer_id = fields

class Worker(object):
    def __init__(self, fields):
        self.worker_id, self.status, self.heart_ip, self.heart_port = fields

class AddrMap(object):
    def __init__(self, fields):
        self.worker_id, self.public_ip, self.ports_allocated, self.port_80_allocated = fields

################################################################################

class PicoManager(object):

    def get_next_internal_ip(self):
        query = ("SELECT last_internal_ip FROM {0} ".format(META_TABLE))
        cursor = self.db.cursor()
        cursor.execute(query)
        last = cursor.fetchone()[0]

        last = last.split('.')
        octets = [int(octet) for octet in last]

        octets[3] += 1
        for i in reversed(range(1,4)):
        if octets[i] > 255:
                octets[i-1] += 1

        new_ip = '{0}.{1}.{2}.{3}'.format(a,b,c,d)

        query ("UPDATE {0} SET last_internal_ip='{1}'".format(META_TABLE, new_ip))
        cursor.execute(query)

        return new_ip

    def new_picoprocess(self, hostname, ports, customer_id):
        """
        Called when user registers a new picoprocess with our service.
        Simply updates the database with information about this picoprocess, and
            initiates its status to cold
        """

        # Choose global internal ip
        internal_ip = self.get_next_internal_ip()
        ports = ';'.join(ports)

        query = ("INSERT INTO {0} SET hot={1},worker_id='',public_ip='',internal_ip={2},ports={3},hostname={4},customer_id={5}".format(PICO_TABLE, False, internal_ip,ports,hostname,customer_id))

        cursor = self.db.cursor()
        cursor.execute(query)

        # Try / catch some sort of error here, return error code
        return 0

    def find_available_worker(self, ports):

        query = ("SELECT ")

        cursor = self.db.cursor()
        cursor.execute(query)

        cursor.fetchone()

        return worker, port_map

    def run_picoprocess(self, pico):
        """
        Called by DNS Resolver when it reiceves a request for a cold process.
        Chooses a suitable machine, informs worker, updates database, then responds
            to DNS query
        """

        worker, port_map = self.find_available_worker(pico.ports)
        args = (pico.pico_id, pico.internal_ip, pico.port_map, 0) # 0 for no flags
        msg = msgpack.packb(args)
        self.socket.send(msg)
        success = socket.recv()
        if not success:
            # EMAIL CUSTOMER!
            return None

        query = ("UPDATE {0} SET hot=TRUE, worker_id='{1}', public_ip='{2}', WHERE pico_id='{3}'".format(PICO_TABLE, worker.worker_id, worker.public_ip,pico.pico_id))
        cursor = self.db.cursor()
        cursor.execute(query)

        return worker.public_ip

    def migrate_picoprocesses(self, bad_worker):
        """
        Called by heart when a machine, bad_worker, has gone down (or
            stopped responding).
        Moves all picoprocesses from bad_worker to some new workers, which
            are not necessarily all the same, depending on port requirements
            of each process
        """

        pass

    def __init__(self, db, socket):
        self.db = db
        self.socket = socket

################################################################################

class HubResolver(object):

    def resolve_hostname(self, hostname):
        query = ("SELECT %s FROM %s WHERE hostname='%s'" %
            (PICO_FIELDS, PICO_TABLE, hostname))
        cursor = self.db.cursor()
        cursor.execute(query)
        result = cursor.fetchone()
        if result:
            pico = Picoprocess(result)
            if pico.hot:
                return pico.public_ip
            else:
                self.pico_manager.run_picoprocess(pico)
                return pico.public_ip
        return None

    def query(self, query, timeout=None):
        """
        Lookup the hostname in our database. If we manage it, make sure it's running,
        then return its public IP address, otherwise fail.
        """

        if query.type == dns.A:
            name = query.name.name
            ip = self.resolve_hostname(name)
            if ip:
                answer = dns.RRHeader(
                    name=name,
                    payload=dns.Record_A(address=ip),
                    ttl=RECORD_TTL)
                answers = [answer]
                return defer.succeed((answers,[],[]))
            else:
                return defer.fail(error.DomainError())

    def lookupAllRecords(self, name, timeout=None):
        # TODO
        return None

    def __init__(self, dbpasswd, id):
        self.db = MySQLdb.connect(host='localhost',user='root',passwd=dbpasswd,db='picocenter')
        self.db.autocommit(True)

        socket = zmq.Context().socket(zmq.DEALER)
        socket.identity = 'dns-' + str(id)
        socket.connect('ipc://frontend.ipc')
        self.socket = socket

        self.pico_manager = PicoManager(self.db, self.socket)

################################################################################

def main(args):
    """
    Run the server.
    """
    factory = server.DNSServerFactory(
        clients=[HubResolver(args[2], args[3]), client.Resolver(resolv='/etc/resolv.conf')]
    )

    protocol = dns.DNSDatagramProtocol(controller=factory)

    reactor.listenUDP(int(args[1]), protocol)
    reactor.listenTCP(int(args[1]), factory)

    reactor.run()

if __name__ == '__main__':
    args = ['port','dbpass','id']
    if len(sys.argv) < len(args)+1:
        sys.exit('usage: python hub-dns.py ' + ' '.join(['['+arg+']' for arg in args]))
    elif sys.argv[1] == '53' and os.getuid() != 0:
        sys.exit('You need to run me as root to bind to port 53!')
    else:
        raise SystemExit(main(sys.argv))