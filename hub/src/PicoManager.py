import MySQLdb
import random
import msgpack

import config
logger = config.logger

################################################################################

class Picoprocess(object):
    def __init__(self, fields):
        self.pico_id, self.hot, self.worker_id, self.public_ip, self.internal_ip, self.ports, self.hostname, self.customer_id = fields

class PicoManager(object):

    def get_next_internal_ip(self):
        query = "SELECT last_internal_ip FROM meta "
        cursor = self.db.cursor()
        cursor.execute(query)
        last = cursor.fetchone()[0]

        last = last.split('.')
        octets = [int(octet) for octet in last]

        octets[3] += 1
        for i in reversed(range(1,4)):
            if octets[i] > 255:
                octets[i-1] += 1

        new_ip = '.'.join([str(o) for o in octets])

        query = "UPDATE meta SET last_internal_ip='{0}'".format(new_ip)
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

        query = "INSERT INTO picos SET hot={0},internal_ip='{1}',ports='{2}',hostname='{3}',customer_id={4}".format(False, internal_ip,ports,hostname,customer_id)
        cursor.execute(query)

        query = "SELECT LAST_INSERT_ID();"
        cursor.execute(query)
        pico_id = cursor.fetchall()[0]

        return pico_id

    def find_available_worker(self, ports):
        ports = ports.split(";")

        logger.debug("looking for ports: {0}".format(ports))

        port_conditions = "pp.port=%s" % ports[0]
        for port in ports[1:]:
            port_conditions += (" OR pp.port=%s" % port)

        query = "SELECT ip, heart_ip, w.worker_id FROM ips i, workers w WHERE i.worker_id=w.worker_id AND status=1 AND (ip NOT IN (SELECT public_ip FROM picos p, picoports pp WHERE p.pico_id=pp.pico_id AND ({0})))".format(port_conditions)
        cursor = self.db.cursor()
        cursor.execute(query)

        host_ip, heart_ip, worker_id = cursor.fetchone()
        return host_ip, heart_ip, worker_id

    def generate_portmap(self, pico):
        public_ip = '192.168.1.50'
        public_port = 4040#random.randint(1000, 65535)
        protocol = 'TCP'
        return "{0}:{1}.{2}={3}:{4}".format(public_ip, public_port, protocol, pico.internal_ip, pico.ports.split(';')[0])


    def run_picoprocess(self, pico):
        """
        Called by DNS Resolver when it reiceves a request for a cold process.
        Chooses a suitable machine, informs worker, updates database, then responds
            to DNS query
        """

        logger.debug("PicoManager: run_picoprocess (pico={0})".format(pico.__dict__))
        host_ip, heart_ip, worker_id  = self.find_available_worker(pico.ports)
        logger.debug("found worker {0}".format(heart_ip))
        portmap = self.generate_portmap(pico)
        logger.debug("pico portmap: {0}".format(portmap))

        pico.public_ip = host_ip
        pico.worker_id = worker_id

        args = (pico.pico_id, pico.internal_ip, portmap, False)
        msg = msgpack.packb(args)
        msg = heart_ip + "|" + msg

        self.socket.send(msg)

        ret = self.socket.recv()
        logger.debug("message recvd: " + str(ret))
        if ret:
            # TODO email customer?
            logger.critical("worker failed to start picoprocess!")
            return None

        query = ("UPDATE picos SET hot=TRUE, worker_id='{0}', public_ip='{1}' WHERE pico_id='{2}'".format(worker_id, pico.public_ip, pico.pico_id))
        cursor = self.db.cursor()
        cursor.execute(query)

    def migrate_picoprocesses(self, bad_worker):
        """
        Called by heart when a machine, bad_worker, has gone down (or
            stopped responding).
        Moves all picoprocesses from bad_worker to some new workers, which
            are not necessarily all the same, depending on port requirements
            of each process
        """
        logger.debug("PicoManager: migrate_picoprocesses({0})".format(bad_worker))

        cursor = self.db.cursor()

        find_orphans = "SELECT * FROM picos WHERE worker_id=(SELECT worker_id FROM workers WHERE heart_ip='{0}')".format(bad_worker)
        cursor.execute(find_orphans)
        orphans = cursor.fetchall()

        logger.debug("Found {0} orphans, relocating...".format(len(orphans)))

        for orphan in orphans:
            self.run_picoprocess(Picoprocess(orphan))

        remove_worker = "DELETE FROM ips,workers USING workers INNER JOIN ips WHERE ip='{0}'".format(bad_worker)
        cursor.execute(remove_worker)


    def __init__(self, db, socket):
        self.db = db
        self.socket = socket
        # test