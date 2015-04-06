import MySQLdb
import random
import msgpack
from PicoManager import PicoManager

db = MySQLdb.connect(host='localhost',user='root',passwd='pewee2brandy',db='picocenter')
db.autocommit(True)

p = PicoManager(db, None)
x = p.new_picoprocess("frank.com",["8000","8080"], 10)
print x

print p.find_available_worker(";8000;8080;")