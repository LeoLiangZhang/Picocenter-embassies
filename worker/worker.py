'''A worker is a manager of picoprocesses. The current worker is built for
KVM picoprocesses.
'''

import sys, os
import time, subprocess, signal

from urlparse import urlparse
import shutil
import urllib2

# OrderedDict
OrderedDict = None
try:
    import collections
    OrderedDict = collections.OrderedDict
except:
    import ordereddict
    OrderedDict = ordereddict.OrderedDict

import boto, boto.utils

import config, iptables, hubproxy
logger = config.logger

######################
## Helper functions ##
######################
def ensure_dir_exit(path):
    if not os.path.isdir(path):
        os.makedirs(path, mode=0775)

def open_input_output(path):
    '''Return a tuple (stdin, stdout, stderr) of file handlers in the path
    directory. Note: stdin is point to /dev/null.
    '''
    stdout_path = os.path.join(path, 'stdout')
    stderr_path = os.path.join(path, 'stderr')
    f_stdin = open('/dev/null', 'rb', 0) # no buffer
    f_stdout = open(stdout_path, 'wb')
    f_stderr = open(stderr_path, 'wb')
    return f_stdin, f_stdout, f_stderr

########################
## Process Management ##
########################
class ProcessBase(object):
    """Provide basic control for process."""
    def __init__(self):
        self._popen = None
        self._args = []
        self._cwd = None
        self._env = None
        self._returncode = None
        self.stop_callback = None

    def append_args(self, obj):
        if isinstance(obj, dict):
            for k, val in obj.iteritems():
                self._args.append(k)
                self._args.append(val)
        elif isinstance(obj, list):
            self._args += obj
        else:
            self._args.append(str(obj))

    def _execute(self, args, stdin=None, stdout=None, stderr=None,
                close_fds=False, cwd=None, env=None):
        self._popen = subprocess.Popen(args, stdin=stdin, stdout=stdout,
                                       stderr=stderr, close_fds=close_fds,
                                       cwd=cwd, env=env)
        logger.debug('exec(%s) => %d', self._args, self._popen.pid)
        ProcessBase._add_process(self)

    def run(self):
        if self._cwd:
            ensure_dir_exit(self._cwd)
        f_stdin, f_stdout, f_stderr = open_input_output(self._cwd)
        self._execute(self._args,
                     stdin=f_stdin, stdout=f_stdout, stderr=f_stderr,
                     close_fds=True, cwd=self._cwd, env=self._env)
        f_stdin.close()
        f_stdout.close()
        f_stderr.close()

    @property
    def pid(self):
        if self._popen:
            return self._popen.pid
        else:
            return -1

    @property
    def stopped(self):
        return self.returncode is not None

    @property
    def returncode(self):
        if self._returncode is not None:
            return self._returncode
        elif self._popen:
            self._returncode = self._popen.poll()
        return self._returncode

    def kill(self):
        try:
            if self._popen:
                self._popen.kill();
        except:
            pass

    def send_signal(self, signal):
        if self._popen:
            self._popen.send_signal(signal)

    @classmethod
    def _setup_sigchld_handler(cls):
        cls._processes = {}
        cls._process_stop_listeners = []
        def sigchld_handler(signum, frame):
            ret = None
            try:
                ret = os.waitpid(-1, os.WNOHANG)
            except OSError as e:
                if e.errno == 10: # No child processes.
                    return
                logger.debug('In sigchld_handler, os.waitpid() raised %s.', e)
                return
            logger.debug('os.waitpid() => %s', ret)
            if ret is None:
                return
            pid, rc = ret
            p = cls._processes.get(pid, None)
            if p:
                p._returncode = rc
                del cls._processes[pid]
                for listener in cls._process_stop_listeners:
                    try:
                        listener(p)
                    except Exception as e:
                        fmt = 'Error raised in process_stop_listener: %s'
                        logger.critical(fmt, e)
                if p.stop_callback:
                    try:
                        p.stop_callback(p)
                    except Exception as e:
                        fmt = 'Error raised in stop_callback: %s'
                        logger.critical(fmt, e)
        signal.signal(signal.SIGCHLD, sigchld_handler)

    @classmethod
    def _add_process(cls, p):
        pid = p.pid
        cls._processes[pid] = p

    @classmethod
    def add_process_stop_listener(cls, listener):
        cls._process_stop_listeners.append(listener)

    @classmethod
    def has_running_process(cls):
        return len(cls._processes) > 0

# Init SIGCHLD signal handler, and process_stop_listener service
ProcessBase._setup_sigchld_handler()


class S3Fetcher(ProcessBase):
    def __init__(self, config, s3url, fullpath, callback=None):
        super(S3Fetcher, self).__init__()
        self.s3url = s3url
        self.fullpath = fullpath
        args = [config.python_bin, config.s3fetch, '-o', fullpath, s3url]
        self.append_args(args)
        self.stop_callback = callback
        self._cwd = os.path.join(os.path.dirname(fullpath), 's3fetch')


class S3Uploader(ProcessBase):
    '''Uploader file to S3 bucket, which can be set int the config.

    The uploader assume all files are in config.worker_var_dir, or its subdir.
    For example, $worker_var_dir/pico/42/kvm.swap will save to
    s3://$s3_bucket/pico/42/kvm.swap.
    '''

    def __init__(self, config, fullpaths, callback=None):
        super(S3Uploader, self).__init__()
        # assert(fullpaths.startswith(config.worker_var_dir))
        self.fullpaths = fullpaths
        args = [config.python_bin, config.s3put, '-b', config.s3_bucket,
                '-p', config.worker_var_dir] + fullpaths
        self.append_args(args)
        self.stop_callback = callback
        self._cwd = os.path.join(os.path.dirname(fullpaths[0]), 's3put')


class S3Manager:
    def __init__(self, config):
        self.config = config
        self.downloaders = {}
        self.uploaders = {}

    def download(self, s3url, callback=None):
        parsed_url = urlparse(s3url)
        path = parsed_url.path
        fullpath = self.config.worker_var_dir + path
        t1 = time.time()
        def _callback(p):
            t2 = time.time()
            del self.downloaders[fetcher]
            logger.debug('S3Fetcher(pid=%s, rc=%s, time=%s): %s saved to %s',
                fetcher.pid, fetcher.returncode, t2-t1, s3url, fullpath)
            if callback:
                callback(fetcher)
        fetcher = S3Fetcher(self.config, s3url, fullpath, callback=_callback)
        self.downloaders[fetcher] = fetcher
        fetcher.run()

    def download_sync(self, s3url, save_to_file=True):
        t1 = time.time()
        parsed_url = urlparse(s3url)
        path = parsed_url.path
        fullpath = self.config.worker_var_dir + path
        f = boto.utils.fetch_file(s3url)
        data = f.read()
        t2 = time.time()
        logger.debug('boto.s3.fetch(url=%s, time=%s, size=%s)',
                     s3url, t2-t1, len(data))
        if save_to_file:
            ensure_dir_exit(os.path.dirname(fullpath))
            with open(fullpath, 'wb') as f2:
                f2.write(data)
        return data


    def upload(self, fullpaths, callback=None):
        t1 = time.time()
        def _callback(p):
            t2 = time.time()
            del self.uploaders[uploader]
            logger.debug('S3Uploader(pid=%s, rc=%s, time=%s): %s',
                uploader.pid, uploader.returncode, t2-t1, fullpaths)
            if callback:
                callback(uploader)
        uploader = S3Uploader(self.config, fullpaths, callback=_callback)
        self.uploaders[uploader] = uploader
        uploader.run()


class Zftp(ProcessBase):
    def __init__(self, config):
        super(Zftp, self).__init__()
        options = {
            '--origin-filesystem': 'true',
            '--origin-reference': 'true',
            '--listen-zftp': 'tunid',
            '--listen-lookup': 'tunid',
            '--index-dir': 'index'
        }
        env = {
            'ZOOG_TUNID': config.tunid
        }
        self.append_args(config.zftp_bin)
        self.append_args(options)
        self._env = env
        self._cwd = config.zftp_dir


class Coordinator(ProcessBase):
    def __init__(self, config):
        super(Coordinator, self).__init__()
        env = {
            'ZOOG_TUNID': config.tunid
        }
        self.append_args(config.coordinator_bin)
        self._env = env
        self._cwd = config.coordinator_dir


class Monitor(ProcessBase):
    def __init__(self, config, pico):
        super(Monitor, self).__init__()
        self.pico = pico
        self.append_args(config.monitor_bin)
        options = {
            '--wait-for-core': 'false',
            '--image-file': config.monitor_image,
            '--pico-id': str(pico.pico_id),
        }
        self.append_args(options)
        if pico.internal_ip:
            self.append_args({'--assign-in-address': pico.internal_ip})
        if pico.resume:
            self.append_args('--resume')
        env = {
            'ZOOG_TUNID': config.tunid
        }
        self._env = env
        self._cwd = os.path.join(config.pico_dir, str(pico.pico_id))

    def checkpoint(self):
        self.send_signal(signal.SIGUSR2)


#################
## Picoprocess ##
#################
class Pico:

    INIT = 0
    RUN = 1
    CHECKPOINT = 2
    STOPPED = 3
    EXIT = 4

    def __init__(self, config, pico_id, internal_ip=None, portmaps=[], resume=False):
        self.config = config
        self.pico_id = pico_id
        self.internal_ip = internal_ip
        self.portmaps = portmaps
        self.resume = resume
        self.monitor = None
        self.status = Pico.INIT
        self.stop_callback = None
        self.checkpoint_callback = None
        # should_alive: None if not set, False if killed, True if ensure_alive
        self.should_alive = None

    def execute(self):
        # if not (self.status != Pico.INIT and self.status != Pico.STOPPED):
        #     return
        if self.monitor:
            return
        self.should_alive = None
        self.monitor = Monitor(self.config, self)
        self.monitor.run()
        self.status = Pico.RUN

    def checkpoint(self):
        if self.monitor and self.status != Pico.STOPPED and \
                            self.status != Pico.CHECKPOINT:
            self.status = Pico.CHECKPOINT
            self.monitor.checkpoint()

    def kill(self):
        # cannot kill a pico in checkpointing
        if self.monitor and self.status == Pico.RUN:
            self.monitor.kill()

    @property
    def pid(self):
        return self.monitor.pid if self.monitor else -1


class PicoManager:
    def __init__(self, config, pico_exit_callback=None):
        self.config = config
        self._picos = {} # pico_id -> pico
        self.pico_exit_callback = pico_exit_callback
        ProcessBase.add_process_stop_listener(self._process_stop_listener)

    def _process_stop_listener(self, p):
        pid = p.pid # p is the monitor
        if not hasattr(p, 'pico'):
            return
        monitor = p
        pico = p.pico
        if pico.status == Pico.CHECKPOINT:
            pico.status = Pico.STOPPED
        else:
            pico.status = Pico.EXIT
        pico.monitor = None
        if pico.checkpoint_callback:
            pico.checkpoint_callback(pico)
            # should I reset checkpoint_callback?
        if pico.status == Pico.STOPPED and pico.should_alive:
            self._resume(pico)
        elif pico.status == Pico.EXIT:
            # got killed or pico exited
            self.pico_exit_callback(pico)
            # self.release(pico.pico_id)

    def get_pico_by_id(self, pico_id):
        return self._picos.get(pico_id, None)

    def _execute(self, pico):
        pico.execute()
        logger.info('pico.execute(pico_id=%d, internal_ip=%s, resume=%s)'+
                    ' => pid=%s', pico.pico_id, pico.internal_ip, pico.resume,
                    pico.pid)

    def _resume(self, pico):
        pico.resume = True
        self._execute(pico)

    def execute(self, pico_id, internal_ip, portmaps, resume):
        pico = self.get_pico_by_id(pico_id)
        if pico is None:
            pico = Pico(self.config, pico_id, internal_ip, portmaps, resume)
            self._picos[pico_id] = pico
            self._execute(pico)
        else:
            msg = 'PicoManager.execute fail: Pico(id=%s, status=%s) exists.'
            # use ensure_alive or release then execute
            logger.warn(msg, pico_id, pico.status)

    def checkpoint(self, pico_id, callback=None):
        pico = self.get_pico_by_id(pico_id)
        if pico is None:
            logger.warn('PicoManager.checkpoint: pico(id=%s) not found.',
                        pico_id)
            return
        pico.checkpoint_callback = callback
        if pico.status == Pico.STOPPED:
            if callback:
                callback(pico)
        else:
            pico.checkpoint()

    def kill(self, pico_id):
        pico = self.get_pico_by_id(pico_id)
        if pico is None:
            logger.warn('PicoManager.kill: pico(id=%s) not found.', pico_id)
            return
        pico.should_alive = False
        pico.kill()
        del self._picos[pico_id]

    def release(self, pico_id):
        '''Try to release the pico.

        Return False if the pico has ensure_alive flag, or True when all
        resources are free.
        '''
        pico = self.get_pico_by_id(pico_id)
        if pico is None:
            logger.warn('PicoManager.release: pico(id=%s) not found.',
                        pico_id)
            return True
        if pico.should_alive:
            return False
        fmt = self.config.pico_path_fmt
        pico_dir = fmt.format(pico_id, '')
        shutil.rmtree(pico_dir)
        del self._picos[pico_id]
        logger.debug('PicoManager.release %s', pico_dir)

    def ensure_alive(self, pico_id):
        # TODO: This funciton needs more testing, especially CHECKPOINT case
        pico = self.get_pico_by_id(pico_id)
        if pico is None:
            logger.warn('PicoManager.ensure_alive: pico(id=%s) not found.',
                        pico_id)
            return # pico not exist, ignore
        if pico.status == Pico.RUN:
            return
        elif pico.status == Pico.STOPPED:
            self._resume(pico)
        elif pico.status == Pico.CHECKPOINT:
            pico.should_alive = True


class PortMapper:
    def __init__(self, picoman):
        self.mapping = {}
        self.picoman = picoman

    def add_mappings(self, pico_id, portmaps):
        for portmap in portmaps:
            port_key = portmap.port_key
            self.mapping[port_key] = pico_id

    def remove_mappings(self, pico_id):
        pico = self.picoman.get_pico_by_id(pico_id)
        portmaps = pico.portmaps
        for portmap in portmaps:
            port_key = portmap.port_key
            del self.mapping[port_key]

    def add_pico(self, pico_id):
        pico = self.picoman.get_pico_by_id(pico_id)
        portmaps = pico.portmaps
        pico_id = pico.pico_id
        self.add_mappings(pico_id, portmaps)
        self.install_dnat(pico_id)

    def remove_pico(self, pico_id):
        self.delete_dnat(pico_id)
        self.delete_log(pico_id) # TODO: is it necessary?
        self.remove_mappings(pico_id)

    def find(self, port_key):
        return self.mapping.get(port_key, None)

    def _dnat_func(self, pico_id, func):
        pico = self.picoman.get_pico_by_id(pico_id)
        portmaps = pico.portmaps
        for portmap in portmaps:
            dip, dport, proto, tip, tport = (portmap.dip, portmap.dport,
                portmap.proto, portmap.tip, portmap.tport)
            func(dip, dport, proto, tip, tport)

    def install_dnat(self, pico_id):
        self._dnat_func(pico_id, iptables.dnat)

    def delete_dnat(self, pico_id):
        self._dnat_func(pico_id, iptables.delete_dnat)

    def _log_func(self, pico_id, func):
        pico = self.picoman.get_pico_by_id(pico_id)
        portmaps = pico.portmaps
        for portmap in portmaps:
            dip, dport, proto = (portmap.dip, portmap.dport, portmap.proto)
            func(dip, dport, proto)

    def install_log(self, pico_id):
        self._log_func(pico_id, iptables.log)

    def delete_log(self, pico_id):
        self._log_func(pico_id, iptables.delete_log)


################
## The Worker ##
################
class Worker:

    def __init__(self, config):
        self.config = config
        self.coordinator = None
        self.zftp = None
        self.has_started = False
        self.is_stopping = False
        self.picoman = PicoManager(config, self.pico_exit_callback)
        self.s3man = S3Manager(config)
        self.portmapper = PortMapper(self.picoman)
        self.resouceman = ResourceManager(self)
        self.hub = None
        self.find_public_ips()

    def find_public_ips(self):
        # TODO: kind of hacky, find a local way of doing this
        self.heart_ip = urllib2.urlopen("http://ipecho.net/plain").read()
        # TODO: find all other IPs managed by this worker and include in self.public_ips
        self.public_ips = [self.heart_ip]

    def set_hub(self, hub):
        self.hub = hub

    def pico_exit_callback(self, pico):
        pico_id = pico.pico_id
        self.picoman.release(pico_id)
        self.resouceman.remove(pico_id)

    def port_callback(self, dip, dport, proto):
        port_key = iptables.get_port_key(dip, dport, proto)
        pico_id = self.portmapper.find(port_key)
        if pico_id:
            self.pico_ensure_alive(pico_id)

    def _check_coordinator_ready(self):
        tunid = self.config.tunid
        cmd = '/sbin/ifconfig |grep 10.{0}.0.1 > /dev/null'.format(tunid)
        args = ['/bin/bash', '-c', cmd]
        return subprocess.call(args) == 0

    def _process_stop_listener(self, p):
        if p == self.coordinator:
            logger.critical('Coordinator has stopped.')
            self.stop()
        elif p == self.zftp:
            logger.critical('Zftp has stopped.')
            # should we restart it?
        if self.coordinator.stopped and self.zftp.stopped:
            exit()

    def pico_exec(self, pico_id, internal_ip, s_portmaps, resume):
        logger.info('pico_exec(%s, %s, %s, %s)',
            pico_id, internal_ip, s_portmaps, resume)
        if resume:
            s3url = 's3://{0}/pico/{1}/{2}'.format(
                self.config.s3_bucket, pico_id, self.config.monitor_swap_file)
            self.s3man.download_sync(s3url)
        portmaps = iptables.convert_portmaps(s_portmaps)
        self.picoman.execute(pico_id, internal_ip, portmaps, resume)
        self.portmapper.add_pico(pico_id)
        self.resouceman.make_hot(pico_id)
        return 0

    def pico_release(self, pico_id):
        logger.info('pico_release(%s)', pico_id)
        def ckpt_callback(pico):
            fmt = self.config.pico_path_fmt
            fullpaths = []
            fullpaths.append(fmt.format(pico_id, self.config.monitor_swap_file))
            fullpaths.append(fmt.format(pico_id, self.config.monitor_swap_page))
            def _release(uploader):
                self.picoman.release(pico_id)
            self.s3man.upload(fullpaths, callback=_release)
        self.portmapper.remove_pico(pico_id)
        self.picoman.checkpoint(pico_id, ckpt_callback)
        self.resouceman.remove(pico_id)

    def pico_nap(self, pico_id):
        logger.info('pico_nap(%s)', pico_id)
        def ckpt_callback(pico):
            pass
        self.portmapper.install_log(pico_id)
        self.picoman.checkpoint(pico_id, ckpt_callback)
        self.resouceman.make_warm(pico_id)

    def pico_kill(self, pico_id):
        logger.info('pico_kill(%s)', pico_id)
        self.portmapper.remove_pico(pico_id)
        self.picoman.kill(pico_id)
        self.resouceman.remove(pico_id)

    def pico_ensure_alive(self, pico_id):
        logger.info('pico_ensure_alive(%s)', pico_id)
        self.picoman.ensure_alive(pico_id)
        self.portmapper.delete_log(pico_id)
        self.resouceman.make_hot(pico_id)

    def start(self):
        if self.has_started:
            return
        config = self.config
        ensure_dir_exit(config.worker_var_dir)
        ProcessBase.add_process_stop_listener(self._process_stop_listener)

        coordinator = Coordinator(config)
        self.coordinator = coordinator
        coordinator.run()
        logger.info('coordinator.pid = %d', coordinator.pid)

        retry = 0
        max_retry = 60
        while retry < max_retry:
            if self._check_coordinator_ready():
                break
            retry += 1
            time.sleep(1)

        zftp = Zftp(config)
        self.zftp = zftp
        zftp.run()
        logger.info('zftp.pid = %d', zftp.pid)
        self.has_started = True
        # self._setup_sigchld_handler()

    def _kill_processes(self, processes):
        for p in processes:
            p.kill()

    def stop(self):
        if self.is_stopping:
            return
        self.is_stopping = True
        self._kill_processes([self.zftp, self.coordinator])


#################
## ResourceMan ##
#################
class ResourceManager:
    def __init__(self, worker):
        self.worker = worker
        self.config = worker.config
        self.hot_picos = OrderedDict()
        self.warm_picos = OrderedDict()
        self.status = hubproxy.WorkerStatus.AVAILABLE

    def make_hot(self, pico_id):
        if pico_id in self.warm_picos:
            del self.warm_picos[pico_id]
        self.hot_picos[pico_id] = True
        self._should_nap()
        self._update_hub_status()

    def make_warm(self, pico_id):
        if pico_id in self.hot_picos:
            del self.hot_picos[pico_id]
        self.warm_picos[pico_id] = True

    def remove(self, pico_id):
        if pico_id in self.hot_picos:
            del self.hot_picos[pico_id]
        if pico_id in self.warm_picos:
            del self.warm_picos[pico_id]

    def _should_nap(self):
        if len(self.hot_picos) > self.config.max_hot_pico_per_worker:
            pico_id = self.hot_picos.popitem(last=False)[0]
            self.worker.pico_nap(pico_id)

    def _update_hub_status(self):
        count = len(self.hot_picos) + len(self.warm_picos)
        status = self.status
        if count > self.config.max_pico_per_worker:
            status = hubproxy.WorkerStatus.OVERLOADED
            if len(self.warm_picos):
                pico_id = self.warm_picos.popitem(last=False)[0]
            else:
                pico_id = self.hot_picos.popitem(last=False)[0]
            self.worker.pico_release(pico_id)
        else:
            status = hubproxy.WorkerStatus.AVAILABLE
        if self.status != status:
            # self.worker.hub.update_worker_status(status)
            logger.info('hub.update_worker_status(%s)', status)
            self.status = status


def main_event_loop():
    _config = config.WorkerConfig()
    _config.load_args()
    logger.debug(_config)
    import zmq, zmq.eventloop
    ioloop = zmq.eventloop.ioloop
    loop = ioloop.ZMQIOLoop()
    loop.install()

    # iptables.dnat('192.168.1.50', 8080, 'tcp', '10.2.0.5', 8080)
    # iptables.log('192.168.1.50', 8080, 'tcp')
    def sigint_handler(sig, frame):
        logger.critical('Receive SIGINT. Stopping...')
        worker.stop()
    signal.signal(signal.SIGINT, sigint_handler)

    import interface

    worker = Worker(_config)
    worker.start()

    HubConnectionClass = hubproxy.HubConnection
    if _config.local_run:
        HubConnectionClass = hubproxy.MockHubConnection
    hub = HubConnectionClass(loop, worker)
    worker.set_hub(hub)
    hub.connect()
    hub.start()

    # proto must be in captital letters
    # worker.pico_exec(5, '10.2.0.5', '192.168.1.50:4040.TCP=10.2.0.5:8080', False)

    server = interface.TcpEvalServer(worker, io_loop=loop)
    server.listen(1234)
    log_listener = iptables.IptablesLogListener()
    log_listener.port_callback = worker.port_callback
    log_listener.bind(loop)

    loop.start()


def main():
    config = config.WorkerConfig()
    logger.debug(config)

    worker = Worker(config)
    worker.start()

    def sigint_handler(sig, frame):
        logger.critical('Receive SIGINT. Stopping...')
        worker.stop()
    signal.signal(signal.SIGINT, sigint_handler)

    import code
    picoman = worker.picoman
    code.interact(local=locals())

def wait_all_processes():
    while True:
        if not ProcessBase.has_running_process():
            break
        time.sleep(1)

def test():
    config = config.WorkerConfig()
    s3man = S3Manager(config)
    s3man.download('s3://elasticity-storage/pico/42/kvm.swap')
    s3man.upload([config.worker_var_dir+'/pico/5/test'])
    wait_all_processes()

if __name__ == '__main__':
    # main()
    main_event_loop()
    # test()
