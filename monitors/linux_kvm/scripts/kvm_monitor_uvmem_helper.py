import logging, sys, signal, os
import struct, mmap, io

# Amazon AWS API
import boto
from boto.s3.key import Key

# my embedded module
import emb

# OrderedDict
OrderedDict = None
try:
    import collections
    OrderedDict = collections.OrderedDict
except:
    import ordereddict
    OrderedDict = ordereddict.OrderedDict


LOGGER_NAME = 'uvmem_page_server'
S3_BUCKET_NAME = 'elasticity-storage'
PAGE_MULTIPLIER = 32 # block_size = page_size * multiplier
CACHE_CAPACITY = 40 # LRUCache capacity

# Set the root logger level to debug will print boto's logging.
# logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger(LOGGER_NAME)
logger.setLevel(logging.DEBUG)
logger.addHandler(logging.StreamHandler(sys.stderr))


# Handle interrupt gracefully
def signal_handler_sigint(signal, frame):
    logger.info('py_uvmem_server receives SIGINT, exit.')
    sys.exit(0)
signal.signal(signal.SIGINT, signal_handler_sigint)


# Init global vars.
logger.debug('hello python')
_tup = emb.get_sarg()
logger.debug('sarg: %s', _tup)
page_fd, uvmem_fd, shmem_fd, map_size, page_size, pico_id = _tup
del _tup


def measure_webpage_load():
    logger.warning('Measure Begin')
    ret = os.fork()
    if ret == 0: # cihld
        import time
        t1 = time.time()
        logger.warning('Measure in Child')
        rc = os.system('bash -c "time curl 10.2.0.5:8080"')
        t2 = time.time()
        logger.warning('PAGE_MULTIPLIER=%d, block_size=%d KB, CACHE_CAPACITY=%d, cache_size=%d KB, TIME=%f sec', 
            PAGE_MULTIPLIER, PAGE_MULTIPLIER*page_size/1024, CACHE_CAPACITY, 
            PAGE_MULTIPLIER*page_size*CACHE_CAPACITY/1024, t2-t1)
        logger.warning('Measure DONE')
# measure_webpage_load()

class LRUCache:
    ''' http://www.kunxi.org/blog/2014/05/lru-cache-in-python/
    '''
    def __init__(self, capacity):
        self.capacity = capacity
        self.cache = OrderedDict()

    def get(self, key):
        try:
            value = self.cache.pop(key)
            self.cache[key] = value
            return value
        except KeyError:
            return None

    def set(self, key, value):
        try:
            self.cache.pop(key)
        except KeyError:
            if len(self.cache) >= self.capacity:
                self.cache.popitem(last=False)
        self.cache[key] = value


class FilePageLoader(object):
    def __init__(self, fd):
        self.f_page = io.open(fd, 'rb')
    
    def load(self, page_file_offset):
        self.f_page.seek(page_file_offset)
        page_data = self.f_page.read(page_size)
        return page_data


class S3PageLoader(object):
    def __init__(self):
        conn = boto.connect_s3()
        bucket_ckpt = conn.get_bucket(S3_BUCKET_NAME)
        key_page = Key(bucket_ckpt, 'nginx2.kvm.swap.page')
        logger.debug('Init elasticity-checkpoint bucket.')
        self.conn = conn
        self.bucket_ckpt = bucket_ckpt
        self.key_page = key_page
        self.block_size = int(page_size) * PAGE_MULTIPLIER # 1M
        self.block_cache = LRUCache(CACHE_CAPACITY) #  cached at most 10M

    def get_block(self, block_num):
        logger.debug("get_block(block=%d)", block_num)
        data = self.block_cache.get(block_num)
        if not data:
            start = block_num * self.block_size
            end = start + self.block_size - 1
            range_str = '{0}-{1}'.format(start, end)
            data = self.key_page.get_contents_as_string(headers={'Range' : 'bytes='+range_str})
            self.block_cache.set(block_num, data)
            logger.debug("Fetch S3 block=%d, size=%d", block_num, len(data))
        return data

    def get_page_data(self, page_file_offset):
        block_num = int(page_file_offset) / self.block_size
        block_offset = int(page_file_offset) % self.block_size
        data = self.get_block(block_num)
        return data[block_offset:block_offset+page_size]

    def load(self, page_file_offset):
        # range_str = '{0}-{1}'.format(page_file_offset, page_file_offset+page_size-1)
        # page_data = self.key_page.get_contents_as_string(headers={'Range' : 'bytes='+range_str})
        page_data = self.get_page_data(page_file_offset)
        return page_data


def serve_pages(loader):
    f_uvmem = io.open(uvmem_fd, 'r+b', buffering=0)
    logger.debug("Open uvmem_fd in python %s", f_uvmem)

    shmem = mmap.mmap(shmem_fd, map_size, flags=mmap.MAP_SHARED,
        prot=mmap.PROT_READ|mmap.PROT_WRITE, offset=0)
    logger.debug('mmap shmem %s', shmem)

    n_pages = 0
    nr_pages = map_size / page_size

    while n_pages < nr_pages:
        data = f_uvmem.read(32*8)
        logger.debug('f_uvmem.read %s %s', len(data), 'bytes')

        i = 0
        while i < len(data):
            pg_data = data[i:i+8] # 8 is the size of longlong
            pg = struct.unpack('Q', pg_data)[0]
            vaddr = pg * page_size + 0x10001000L
            page_file_offset = emb.find_page_file_offset(vaddr)
            logger.debug('find_page_file_offset(page=%s, vaddr=%s) = %s', 
                hex(pg), hex(vaddr), page_file_offset)

            page_data = loader.load(page_file_offset)
            assert(len(page_data) == page_size)

            shmem_offset = pg * page_size
            shmem.seek(shmem_offset)
            shmem.write(page_data)
            # logger.debug('Write to shmem at %s', shmem_offset)

            f_uvmem.write(pg_data)
            logger.debug('f_uvmem.write(page=%s)', hex(pg))

            i += 8

serve_pages(FilePageLoader(page_fd))
# serve_pages(S3PageLoader())
