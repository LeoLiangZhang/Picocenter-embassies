'''A worker is a manager of picoprocesses. The current worker is built for
KVM picoprocesses.
'''

import sys, os
import time, subprocess, signal
import logging

EMBASSIES_ROOT = '/elasticity/embassies'
LOGGER_NAME = 'picocenter_worker'
LOGGER_FORMAT = '%(asctime)s - %(levelname)s - %(message)s'


# Set the root logger level to debug will print boto's logging.
# logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger(LOGGER_NAME)
logger.setLevel(logging.DEBUG)
handler = logging.StreamHandler(sys.stdout)
formatter = logging.Formatter(LOGGER_FORMAT)
handler.setFormatter(formatter)
logger.addHandler(handler)
del handler, formatter


######## Helper functions #########
def ensure_dir_exit(path):
    if not os.path.isdir(path):
        os.makedirs(path, mode=0775)

def open_input_output(path):
    '''Return a tuple (stdin, stdout, stderr) of file handlers in the path
    directory. Note: stdin is point to /dev/null.
    '''
    stdout_path = os.path.join(path, 'stdout')
    stderr_path = os.path.join(path, 'stderr')
    f_stdin = open('/dev/null', 'rb', 0)
    f_stdout = open(stdout_path, 'wb')
    f_stderr = open(stderr_path, 'wb')
    return f_stdin, f_stdout, f_stderr

########## Helper ends ############


class ConfigBase:
    def __init__(self, **kwargs):
        for k, val in kwargs.iteritems():
            setattr(self, k, val)

    def __str__(self):
        lst = dir(self)
        result = self.__class__.__name__
        tups = ['{0}={1}'.format(s, repr(getattr(self, s))) for s in lst 
                if not s.startswith('_')]
        result += '(' + ', '.join(tups) + ')'
        return result


class WorkerConfig(ConfigBase):
    '''The type for all item value is str.
    '''
    worker_var_dir = os.path.join(EMBASSIES_ROOT, 'var', 'worker')
    tunid = '2'

    zftp_dir = os.path.join(worker_var_dir, 'zftp')
    coordinator_dir = os.path.join(worker_var_dir, 'coordinator')
    pico_dir = os.path.join(worker_var_dir, 'pico')

    zftp_bin = os.path.join(EMBASSIES_ROOT, 'toolchains/linux_elf',
                            'zftp_backend/build/zftp_backend')
    coordinator_bin = os.path.join(EMBASSIES_ROOT, 'monitors/linux_kvm',
                                   'coordinator/build/zoog_kvm_coordinator')
    monitor_bin = os.path.join(EMBASSIES_ROOT, 'monitors/linux_kvm',
                               'monitor/build/zoog_kvm_monitor')
    monitor_image = os.path.join(EMBASSIES_ROOT, 'toolchains/linux_elf',
                                 'elf_loader/build/elf_loader.nginx.signed')


class ProcessBase(object):
    """Provide basic control for process."""
    def __init__(self):
        self._popen = None
        self._args = []
        self._cwd = None
        self._env = None
        self._returncode = None

    def append_args(self, obj):
        if isinstance(obj, dict):
            for k, val in obj.iteritems():
                self._args.append(k)
                self._args.append(val)
        elif isinstance(obj, list):
            self._args += obj
        else:
            self._args.append(str(obj))

    def execute(self, args, stdin=None, stdout=None, stderr=None,
                close_fds=False, cwd=None, env=None):
        self._popen = subprocess.Popen(args, stdin=stdin, stdout=stdout,
                                       stderr=stderr, close_fds=close_fds,
                                       cwd=cwd, env=env)
        ProcessBase._add_process(self)

    def run(self):
        ensure_dir_exit(self._cwd)
        f_stdin, f_stdout, f_stderr = open_input_output(self._cwd)
        logger.debug(self._args)
        self.execute(self._args, 
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

    @classmethod
    def _setup_sigchld_handler(cls):
        cls._processes = {}
        cls._process_stop_listeners = []
        def sigchld_handler(signum, frame):
            ret = None
            try:
                ret = os.waitpid(-1, os.WNOHANG)
            except OSError as e:
                logger.warn('In sigchld_handler, os.waitpid() raised %s.', e)
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
        signal.signal(signal.SIGCHLD, sigchld_handler)

    @classmethod
    def _add_process(cls, p):
        pid = p.pid
        cls._processes[pid] = p

    @classmethod
    def add_process_stop_listener(cls, listener):
        cls._process_stop_listeners.append(listener)

# Init SIGCHLD signal handler, and process_stop_listener service
ProcessBase._setup_sigchld_handler()

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
    def __init__(self, config, pico_id, internal_ip, resume):
        super(Monitor, self).__init__()
        options = {
            '--wait-for-core': 'false',
            '--image-file': config.monitor_image,
            '--pico-id': str(pico_id),
            '--assign-in-address': internal_ip,
        }
        env = {
            'ZOOG_TUNID': config.tunid
        }
        self.append_args(config.monitor_bin)
        self.append_args(options)
        if resume:
            self.append_args('--resume')
        self._env = env
        self._cwd = os.path.join(config.pico_dir, str(pico_id))



class Worker:

    def __init__(self, config):
        self.config = config
        self.coordinator = None
        self.zftp = None
        self.has_started = False
        self.is_stopping = False

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

    def pico_exec(self, pico_id, internal_ip, resume):
        monitor = Monitor(self.config, pico_id, internal_ip, resume)
        monitor.run()
        logger.info('monitor.pid = %d', monitor.pid)
        return monitor

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


def main():
    config = WorkerConfig()
    logger.debug(config)

    worker = Worker(config)
    worker.start()

    import code
    code.interact(local=locals())

if __name__ == '__main__':
    main()
