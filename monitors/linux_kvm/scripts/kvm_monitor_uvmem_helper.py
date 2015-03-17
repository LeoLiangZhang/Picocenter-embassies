import struct, mmap, io
import boto
from boto.s3.key import Key
import logging, sys
# my embedded module
import emb

# Set the root logger level to debug will print boto's logging.
# logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger('uvmem_page_server')
logger.setLevel(logging.DEBUG)
logger.addHandler(logging.StreamHandler(sys.stderr))

# Init global vars.
logger.debug('hello python')
_tup = emb.get_sarg()
logger.debug('sarg: %s', _tup)
page_fd, uvmem_fd, shmem_fd, map_size, page_size = _tup
del _tup


conn = boto.connect_s3()
bucket_ckpt = conn.get_bucket('elasticity-storage')
key_page = Key(bucket_ckpt, 'nginx2.kvm.swap.page')
logger.debug('Init elasticity-checkpoint bucket.')


def page_server():
    f_page = io.open(page_fd, 'rb')
    f_uvmem = io.open(uvmem_fd, 'r+b', buffering=0)
    logger.debug("open uvmem_fd in python %s", f_uvmem)

    shmem = mmap.mmap(shmem_fd, map_size, flags=mmap.MAP_SHARED,
        prot=mmap.PROT_READ|mmap.PROT_WRITE, offset=0)
    logger.debug("mmap shmem %s", shmem)

    n_pages = 0
    nr_pages = map_size / page_size

    while n_pages < nr_pages:
        data = f_uvmem.read(32*8)
        logger.debug('f_uvmem.read %s %s', len(data), 'bytes')

        i = 0
        while i < len(data):
            pg_data = data[i:i+8]
            pg = struct.unpack('Q', pg_data)[0]
            vaddr = pg * page_size + 0x10001000L
            logger.debug('request uvmem pg %s %s %s', hex(pg), 'vaddr', hex(vaddr))
            page_file_offset = emb.find_page_file_offset(vaddr)
            logger.debug('find_page_file_offset %s', page_file_offset)
            
            # f_page.seek(page_file_offset)
            # page_data = f_page.read(page_size)
            range_str = '{0}-{1}'.format(page_file_offset, page_file_offset+page_size-1)
            page_data = key_page.get_contents_as_string(headers={'Range' : 'bytes='+range_str})
            logger.debug('f_page.read %s %s', len(page_data), 'bytes')

            shmem_offset = pg * page_size
            shmem.seek(shmem_offset)
            shmem.write(page_data)
            logger.debug('write to shmem at %s', shmem_offset)

            f_uvmem.write(pg_data)
            logger.debug('inform uvmem page loaded')

            i += 8

page_server()
