from twisted.internet import reactor, defer
from twisted.names import client, dns, error, server
import MySQLdb
import sys, os

RECORD_TTL = 0
PICOPROCESS_TABLE = 'picos'
WORKER_TABLE = 'workers'
ADDR_TABLE = 'addrmap'

class HubResolver(object):

    def lookuphost(self, hostname):
        query = ("SELECT * FROM %s WHERE hostname='%s'" %
            (PICOPROCESS_TABLE, hostname))
        cursor = self.db.cursor()
        cursor.execute(query)
        result = cursor.fetchone()
        if result:
            hot, public_ip = result[1], result[3]
            if hot == True:
                return public_ip
            else:
                # Create new process
                return None
        return None

    def query(self, query, timeout=None):
        """
        Check if the query should be answered dynamically, otherwise dispatch to
        the fallback resolver.
        """

        if query.type == dns.A:
            name = query.name.name
            ip = self.lookuphost(name)
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

    def __init__(self, dbpasswd):
        self.db = MySQLdb.connect(host='localhost',user='root',passwd=dbpasswd,db='picocenter')
        self.db.autocommit(True)

def main(args):
    """
    Run the server.
    """
    factory = server.DNSServerFactory(
        clients=[HubResolver(args[2]), client.Resolver(resolv='/etc/resolv.conf')]
    )

    protocol = dns.DNSDatagramProtocol(controller=factory)

    reactor.listenUDP(int(args[1]), protocol)
    reactor.listenTCP(int(args[1]), factory)

    reactor.run()


if __name__ == '__main__':
    args = ['port','dbpass']
    if len(sys.argv) < len(args)+1:
        sys.exit('usage: python hub-dns.py ' + ' '.join(['['+arg+']' for arg in args]))
    elif sys.argv[1] == '53' and os.getuid() != 0:
        sys.exit('You need to run me as root to bind to port 53!')
    else:
        raise SystemExit(main(sys.argv))