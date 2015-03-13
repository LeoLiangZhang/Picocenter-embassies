import struct, mmap, io
import emb

print 'hello python'

tup = emb.get_sarg()
print 'sarg: ', tup
page_fd, uvmem_fd, shmem_fd, map_size, page_size = tup

f_page = io.open(page_fd, 'rb')
f_uvmem = io.open(uvmem_fd, 'r+b', buffering=0)
print "open uvmem_fd in python", f_uvmem

shmem = mmap.mmap(shmem_fd, map_size, flags=mmap.MAP_SHARED,
    prot=mmap.PROT_READ|mmap.PROT_WRITE, offset=0)
print "mmap shmem", shmem

n_pages = 0
nr_pages = map_size / page_size

while n_pages < nr_pages:
    data = f_uvmem.read(32*8)
    print 'f_uvmem.read', len(data), 'bytes'

    i = 0
    while i < len(data):
        pg_data = data[i:i+8]
        pg = struct.unpack('Q', pg_data)[0]
        vaddr = pg * page_size + 0x10001000L
        print 'request uvmem pg', hex(pg), 'vaddr', hex(vaddr)
        page_file_offset = emb.find_page_file_offset(vaddr)
        print 'find_page_file_offset', page_file_offset
        
        f_page.seek(page_file_offset)
        page_data = f_page.read(page_size)
        print 'f_page.read', len(page_data), 'bytes'

        shmem_offset = pg * page_size
        shmem.seek(shmem_offset)
        shmem.write(page_data)
        print 'write to shmem at', shmem_offset 

        f_uvmem.write(pg_data)
        print 'inform uvmem page loaded'

        i += 8
