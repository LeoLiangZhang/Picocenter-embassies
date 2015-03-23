import MySQLdb
import random
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

# True = resume, false = fresh 

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

        cursor = self.db.cursor()

        internal_ip = self.get_next_internal_ip()
        ports = ';'.join(ports)

        query = ("INSERT INTO {0} SET hot={1},worker_id='',public_ip='',internal_ip={2},ports={3},hostname={4},customer_id={5}".format(PICO_TABLE, False, internal_ip,ports,hostname,customer_id))
        cursor.execute(query)

        query ("SELECT pico_id FROM {0} WHERE internal_ip='{1}'".format(PICOPROCESS_TABLE, internal_ip))
        cursor.execute(query)
        pico_id = cursor.fetchall()[0]

        return pico_id

    def find_available_worker(self, ports):
	port_conditions = "pp.port=%s" % ports[0]
	for port in ports[1:]:
		port_conditions += (" OR pp.port=%s" % port)
	query = "SELECT ip, heart_ip FROM ips i, workers w WHERE i.worker_id=w.worker_id AND status=1 AND (ip NOT IN (SELECT public_ip FROM picos p, picoports pp WHERE p.pico_id=pp.pico_id AND ({0})))".format(port_conditions)

        cursor = self.db.cursor()
        cursor.execute(query)

        host_ip, worker_ip = cursor.fetchone()
	worker_identity = worker_ip + '\jobs'

        return host_ip, worker_identity

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
        reply = socket.recv()
        success = msgpack.unpackb(reply)
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
