import sys, signal, os, time
import struct, mmap, io
import logging, datetime

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


S3_BUCKET_NAME = 'elasticity-storage'
PICO_SWAP_FILE_FMT = '/pico/{pico_id}/kvm.swap.page'
PAGE_MULTIPLIER = 32 # block_size = page_size * multiplier
CACHE_CAPACITY = 40 # LRUCache capacity
PREEMPTIVE_FETCHING = False
PREEMPTIVE_FETCHING_SIZE = 256
PREEMPTIVE_FETCHING_FILENAME = 'precache.list'

LOGGER_NAME = 'uvmem_page_server'
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
logger = logging.getLogger(LOGGER_NAME)
logger.setLevel(logging.DEBUG)
handler = logging.StreamHandler(sys.stderr)
formatter = MicrosecondFormatter(LOGGER_FORMAT, datefmt=LOGGER_DATEFMT)
handler.setFormatter(formatter)
logger.addHandler(handler)
del handler, formatter


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


# Performance measurement
accumlated_fetching_time = .0


def _measure_webpage_load():
    logger.warning('Measure Begin')
    # ret = os.fork()
    ret = 0
    if ret == 0: # child
        t1 = time.time()
        logger.warning('Measure in Child')
        # rc = os.system('bash -c "time curl 10.2.0.5:8080"')
        import urllib2
        response = urllib2.urlopen('http://10.2.0.5:8080/')
        html = response.read()
        print html
        t2 = time.time()
        global accumlated_fetching_time
        logger.warning('PAGE_MULTIPLIER=%d, block_size=%d KB, CACHE_CAPACITY=%d, cache_size=%d KB,'+
            ' TIME=%f sec, FETCH_TIME=%f sec', 
            PAGE_MULTIPLIER, PAGE_MULTIPLIER*page_size/1024, CACHE_CAPACITY, 
            PAGE_MULTIPLIER*page_size*CACHE_CAPACITY/1024, 
            t2-t1, accumlated_fetching_time)
        logger.warning('Measure DONE')

def measure_webpage_load():
    import threading
    t = threading.Thread(target=_measure_webpage_load)
    # should I use daemon thread?
    t.start()

class LRUCache:
    ''' http://www.kunxi.org/blog/2014/05/lru-cache-in-python/
    '''
    def __init__(self, capacity):
        self.capacity = capacity
        self.cache = OrderedDict()

    def exists(self, key):
        return key in self.cache

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
        t1 = time.time()
        page_data = self.f_page.read(page_size)
        t2 = time.time()
        ts = t2 - t1

        global accumlated_fetching_time
        accumlated_fetching_time += ts
        logger.debug("page_size disk read: time=%f s, acc_time=%f s", 
                     ts, accumlated_fetching_time)
        return page_data


class S3PageLoader(object):
    def __init__(self, page_multiplier=PAGE_MULTIPLIER,
                       cache_capacity=CACHE_CAPACITY):
        conn = boto.connect_s3()
        bucket_ckpt = conn.get_bucket(S3_BUCKET_NAME)
        filename = PICO_SWAP_FILE_FMT.format(pico_id=pico_id)
        key_page = Key(bucket_ckpt, filename)
        logger.debug('Init elasticity-checkpoint bucket.')
        self.conn = conn
        self.bucket_ckpt = bucket_ckpt
        self.key_page = key_page
        self.block_size = int(page_size) * page_multiplier
        self.block_cache = LRUCache(cache_capacity)

    def get_block(self, block_num):
        logger.debug("get_block(block=%d)", block_num)
        data = self.block_cache.get(block_num)
        if not data:
            start = block_num * self.block_size
            end = start + self.block_size - 1
            range_str = '{0}-{1}'.format(start, end)
            t1 = time.time()
            data = self.key_page.get_contents_as_string(headers={'Range' : 'bytes='+range_str})
            t2 = time.time()
            ts = t2 - t1
            global accumlated_fetching_time
            accumlated_fetching_time += ts
            self.block_cache.set(block_num, data)
            logger.debug("Fetch S3 block=%d, size=%d, time=%f s, acc_time=%f s", 
                block_num, len(data), ts, accumlated_fetching_time)
        return data

    def get_page_data(self, page_file_offset):
        block_num = int(page_file_offset) / self.block_size
        block_offset = int(page_file_offset) % self.block_size
        data = self.get_block(block_num)
        if self.block_size == page_size: 
            # optimize page-size block, reduce copy overhead
            return data 
        return data[block_offset:block_offset+page_size]

    def exists(self, page_file_offset):
        block_num = int(page_file_offset) / self.block_size
        return self.block_cache.exists(block_num)

    def load(self, page_file_offset):
        # range_str = '{0}-{1}'.format(page_file_offset, page_file_offset+page_size-1)
        # page_data = self.key_page.get_contents_as_string(headers={'Range' : 'bytes='+range_str})
        page_data = self.get_page_data(page_file_offset)
        return page_data

class PreemptiveCache:
    def __init__(self, size=PREEMPTIVE_FETCHING_SIZE,
                 filename=PREEMPTIVE_FETCHING_FILENAME):
        self.size = size
        self.cache = S3PageLoader(page_multiplier=1, cache_capacity=size)
        self.request_queue = []
        self.has_saved = False
        self.filename = filename

    def prefetch(self):
        if os.path.exists(self.filename):
            logger.debug('PreemptiveCache.prefetch(%s): begin', self.filename)
            with open(self.filename) as f:
                data = f.read()
                lst = map(int, data.split(','))
                for page_file_offset in lst:
                    self.cache.load(page_file_offset)
                logger.debug('PreemptiveCache.prefetch(%s): loaded %s pages',
                    self.filename, len(lst))
        else:
            logger.debug('PreemptiveCache.prefetch fail: %s file not found.',
                         self.filename)

    def load(self, page_file_offset):
        ''' Return data if the page is in pre-cache, otherwise return None.
        '''
        if len(self.request_queue) < self.size:
            self.request_queue.append(page_file_offset)
            if len(self.request_queue) == self.size:
                with open(self.filename, 'w+') as f:
                    f.write(','.join(map(str, self.request_queue)))
                logger.debug('PreemptiveCache saved %s pages to %s.', 
                             len(self.request_queue), self.filename)
        if self.cache.exists(page_file_offset):
            return self.cache.load(page_file_offset)
        return None


def serve_pages(loader, pre_cache=None):
    ''' The main entry point of the serving process.
    '''
    f_uvmem = io.open(uvmem_fd, 'r+b', buffering=0)
    logger.debug("Open uvmem_fd in python %s", f_uvmem)

    shmem = mmap.mmap(shmem_fd, map_size, flags=mmap.MAP_SHARED,
        prot=mmap.PROT_READ|mmap.PROT_WRITE, offset=0)
    logger.debug('mmap shmem %s', shmem)

    n_pages = 0
    nr_pages = map_size / page_size

    if pre_cache:
        pre_cache.prefetch()

    accumlated_paging_time = .0

    while n_pages < nr_pages:
        data = f_uvmem.read(32*8)
        logger.debug('f_uvmem.read %s %s', len(data), 'bytes')

        pg_set = set()

        i = 0
        while i < len(data):
            t1 = time.time()
            pg_data = data[i:i+8] # 8 is the size of longlong
            pg = struct.unpack('Q', pg_data)[0]

            i += 8
            if pg in pg_set:
                continue
            pg_set.add(pg)

            vaddr = pg * page_size + 0x10001000L
            page_file_offset = emb.find_page_file_offset(vaddr)
            logger.debug('find_page_file_offset(page=%s, vaddr=%s) = %s', 
                hex(pg), hex(vaddr), page_file_offset)

            page_data = None
            if pre_cache:
                page_data = pre_cache.load(page_file_offset)
            if page_data is None:
                page_data = loader.load(page_file_offset)
            assert(len(page_data) == page_size)

            shmem_offset = pg * page_size
            shmem.seek(shmem_offset)
            shmem.write(page_data)
            # logger.debug('Write to shmem at %s', shmem_offset)

            f_uvmem.write(pg_data)
            t2 = time.time()
            ts = t2-t1
            accumlated_paging_time += ts
            logger.debug('f_uvmem.write(page=%s): time=%f s, accumlated_paging_time=%f', 
                hex(pg), ts, accumlated_paging_time)


# measure_webpage_load()
if page_fd >= 0:
    serve_pages(FilePageLoader(page_fd))
else:
    if PREEMPTIVE_FETCHING:
        serve_pages(S3PageLoader(), PreemptiveCache())
    else:
        serve_pages(S3PageLoader())
