from HubConnection import HubConnection, WorkerStatus
import json
import sys

class DummyWorker(object):

    # Called by hub
    def pico_exec(self, pico_id, private_ip, port_map, flags):
        pass

    def set_hub(self, hub):
        self.hub = hub

    def pico_release(self, pico_id):
        self.hub.pico_release(pico_id)

    def pico_kill(self, pico_id):
        self.hub.pico_kill(pico_id)

    def update_status(self, status):
        self.hub.update_worker_status(status)

    def __init__(self):
        pass

# TODO: Might want to handle error on open here
with open('hub.config') as f:
    config = json.loads(f.read())

worker = Worker()
hub = HubConnection(config)

hub.set_worker(worker)
worker.set_hub(hub)

ret = hub.connect(config, WorkerStatus.AVAILABLE)
if ret < 0: # Connect failed
    sys.exit("Couldn't connect to hub")

# ...
# ...
# ...

worker.pico_release("123456")
worker.pico_kill("1234")
worker.update_worker_status(WorkerStatus.OVERLOADED)

