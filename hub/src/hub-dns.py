from twisted.internet import reactor, defer
from twisted.names import client, dns, error, server
import MySQLdb
import sys, os
import random
import zmq
import msgpack
from PicoManager import PicoManager

################################################################################

RECORD_TTL = 0
PICOPROCESS_TABLE = 'picos'
PICO_FIELDS = "pico_id,hot,worker_id,public_ip,internal_ip,ports,hostname,customer_id"

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

class HubResolver(object):

    def resolve_hostname(self, hostname):
        query = ("SELECT %s FROM %s WHERE hostname='%s'" %
            (PICO_FIELDS, PICOPROCESS_TABLE, hostname))
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
        socket.identity = 'dns-' + str(id).zfill(2)
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