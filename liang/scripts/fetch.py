import requests
import sys, time, math
import numpy as np

download_list = [
'file-1k',
'file-4k',
'file-16k',
'file-64k',
'file-256k',
'file-1m',
'file-4m',
'file-16m',
'file-64m',
'file-256m'
]

download_list = [
'file-1k',
#'file-4k',
#'file-16k',
]

domain = "10.2.0.2" if len(sys.argv) <= 1 else sys.argv[1]
url_fmt = "http://"+domain+":8080/data/{}"
count = 10

def getms():
    return int(round(time.time() * 1000))

def download(url):
    t0 = getms()
    r = requests.get(url)
    sz = len(r.text)
    t1 = getms()
    # print >>sys.stderr, sz
    return sz, t1-t0

print '# size c_min min max c_max median' 
for i in download_list:
    url = url_fmt.format(i)
    lst = []
    print >>sys.stderr, "Downloading", url
    for j in range(count):
        sz = 0; t = 0
        try:
            sz, t = download(url)
        except Exception as e:
            print >>sys.stderr, e
            continue
        lst.append(t)
        #time.sleep(1)

    n = len(lst)
    std = np.std(lst)
    u = np.mean(lst)
    print sz, u-1.96*std/math.sqrt(n), min(lst), max(lst), u+1.96*std/math.sqrt(n), np.median(lst)

