import os, sys
import logging, datetime

EMBASSIES_ROOT = '/elasticity/embassies'
LOGGER_NAME = 'picocenter_worker'
IPTABLES_HELPER_PATH = os.path.join(EMBASSIES_ROOT, 'worker',
                               'iptables_helper/build/iptables_helper')
IPTABLES_ARG_LOG_PREFIX = "--log-prefix=[embassies_iptables]"

LOGGER_FORMAT = '%(asctime)s - %(levelname)s - %(message)s'
LOGGER_DATEFMT= '%Y-%m-%d %H:%M:%S.%f'

class MicrosecondFormatter(logging.Formatter):
    converter=datetime.datetime.fromtimestamp
    def formatTime(self, record, datefmt=None):
        ct = self.converter(record.created)
        if datefmt:
            s = ct.strftime(datefmt)
        else:
            t = ct.strftime("%Y-%m-%d %H:%M:%S")
            s = "%s,%06d(%f)" % (t, ct.microsecond, record.created)
        return s

# Set the root logger level to debug will print boto's logging.
# logging.basicConfig(level=logging.DEBUG)
logger = None
if not logger:
    logger = logging.getLogger(LOGGER_NAME)
    # pyzmq will call logging.basicConfig(), which makes log record print twice
    logger.propagate = False
    logger.setLevel(logging.DEBUG)
    handler = logging.StreamHandler(sys.stdout)
    formatter = MicrosecondFormatter(LOGGER_FORMAT, datefmt=LOGGER_DATEFMT)
    handler.setFormatter(formatter)
    logger.addHandler(handler)
    del handler, formatter
logging.basicConfig()


############
## Config ##
############
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

    def load_args(self, args=None):
        if args is None:
            args = sys.argv
        i = 0
        while i < len(args):
            arg = args[i]
            if arg.startswith('--'):
                opt = arg[2:].replace('-', '_')
                i += 1
                arg = args[i]
                val = arg
                if hasattr(self, opt):
                    default = getattr(self, opt)
                    t = type(default)
                    val = t(arg)
                setattr(self, opt, val)
            i += 1

class WorkerConfig(ConfigBase):
    '''The type for all item value is str.
    '''
    worker_var_dir = os.path.join(EMBASSIES_ROOT, 'var', 'worker')
    tunid = '2'

    zftp_dir = os.path.join(worker_var_dir, 'zftp')
    coordinator_dir = os.path.join(worker_var_dir, 'coordinator')
    pico_dir = os.path.join(worker_var_dir, 'pico')
    pico_path_fmt = os.path.join(pico_dir, '{0}', '{1}')
    # /$pico_dir/{0:pico_id}/{1:file}

    zftp_bin = os.path.join(EMBASSIES_ROOT, 'toolchains/linux_elf',
                            'zftp_backend/build/zftp_backend')
    coordinator_bin = os.path.join(EMBASSIES_ROOT, 'monitors/linux_kvm',
                                   'coordinator/build/zoog_kvm_coordinator')
    monitor_bin = os.path.join(EMBASSIES_ROOT, 'monitors/linux_kvm',
                               'monitor/build/zoog_kvm_monitor')
    monitor_image = os.path.join(EMBASSIES_ROOT, 'toolchains/linux_elf',
                                 'elf_loader/build/elf_loader.nginx.signed')
    monitor_swap_file = 'kvm.swap'
    monitor_swap_page = 'kvm.swap.page'

    python_bin = '/usr/bin/python'
    s3put = '/usr/local/bin/s3put'
    s3fetch = '/usr/local/bin/fetch_file'
    s3_bucket = 'elasticity-storage'

    max_pico_per_worker = 4
    worker_available_threshold = 3
    max_hot_pico_per_worker = 2

    log_udp_port = 12345
    log_bind_ip = '127.0.0.1'

    eval_tcp_port = 1234

    hub_ip = '0.0.0.0'
    hub_port = '9997'

    local_run = False
    eth0addr = '10.0.0.3'

def main():
    # args = ['--local-run', 'true']
    config = WorkerConfig()
    config.load_args()
    print config

if __name__ == '__main__':
    main()
