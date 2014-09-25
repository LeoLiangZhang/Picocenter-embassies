#! /usr/bin/env python 
import sys, os, time, signal
import subprocess
import shutil
import tornado.ioloop
import tornado.web
import tornado.tcpserver
from tornado.process import Subprocess 

KVM_MONITOR_PATH = "/elasticity/embassies/monitor/build/zoog_kvm_monitor"
KVM_RUN_DIR = "/elasticity/embassies/run"
TUNID = 2
WEB_PORT = 8000

# def call_process(args, stdindata=None, *_args, **_kwargs):
#     ''' Execute a process with stdindata and return its code as well as 
#         std{out/err} data.

#     Return:
#         (returncode, stdoutdata, stderrdata)
#     '''
#     stdin = subprocess.PIPE if stdindata is not None else None
#     p = subprocess.Popen(args, stdin=stdin, \
#                                stdout=subprocess.PIPE, \
#                                stderr=subprocess.PIPE, \
#                                *_args, **_kwargs)
#     stdoutdata, stderrdata = p.communicate(stdindata)
#     return p, p.returncode, stdoutdata, stderrdata

# class PicoRunner(threading.Thread):

#     def execute(self, image_path, tunid = TUNID):
#         self._process = None
#         self.returncode = None
#         self.stdoutdata = None
#         self.stderrdata = None
#         self.image_path = image_path
#         self.tunid = tunid

#     def run(self):
#         if not _process:
#             args = [KVM_MONITOR_PATH, "--image-file", self.image_path, "--wait-for-core", "false"]
#             env = {"ZOOG_TUNID": self.tunid}
#             stdindata = ""
#             self._process, self.returncode, self.stdoutdata, self.stderrdata = call_process(args, stdindata, env=env)

def make_name(prefix="", postfix=""):
    return prefix + "{0:.6f}".format(time.time()).replace('.', '-') + postfix

class NativeProcess(object):

    def __init__(self, cmd_args, cwd=None, env=None, *args, **kwargs):
        self.cmd_args = cmd_args
        self.cwd = cwd
        self.env = env
        self.args = args
        self.kwargs = kwargs
        self._process = None
        self._exit_callbacks = []
        self.tag = None
        self.stdin = None
        self.stdout = None
        self.stderr = None

    def execute(self):
        # print (self.cmd_args, self.cwd, self.env)
        self._process = Subprocess(self.cmd_args, stdin=Subprocess.STREAM, stdout=Subprocess.STREAM, stderr=Subprocess.STREAM, cwd=self.cwd, env=self.env)
        self._process.set_exit_callback(self.on_exit)
        for attr in ['stdin', 'stdout', 'stderr', 'pid']:
            if not hasattr(self, attr):  # don't clobber streams set above
                setattr(self, attr, getattr(self._process, attr))

    def add_exit_callback(self, callback):
        ''' Add callback for process exit event. The callback should take one argument, which is the Process object.
        '''
        self._exit_callbacks.append(callback)

    def remove_exit_callback(self, callback):
        self._exit_callbacks.remove(callback)

    def on_exit(self, returncode):
        for callback in self._exit_callbacks:
            callback(self)

    def get_returncode(self):
        return self._process.returncode


class PicoProcess(NativeProcess):
    def __init__(self, image_path=None, core_path=None, cwd=None, tunid=TUNID):
        self.image_path = image_path
        self.core_path = core_path
        cmd_args = [KVM_MONITOR_PATH, "--wait-for-core", "false"]
        self.is_resume = is_resume
        # copy cores
        if core_path:
            cmd_args += ["--core-file", "kvm.swap"]
        else:
            cmd_args += ["--image-file", image_path]
        env = {"ZOOG_TUNID": tunid}
        self.env = env
        # if not cwd:
        #     cwd = KVM_RUN_DIR + "/" + make_name()
        self.cwd = cwd
        super(PicoProcess, self).__init__(cmd_args, cwd=cwd, env=env)

    def on_exit(self):
        print "PicoProcess.on_exit", self.pid
        super(PicoProcess, self).on_exit()

    def checkpoint(self):
        if self.get_returncode() is not None:
            os.kill(self.pid, signal.SIGUSR2)


on_data = lambda data: None

class Job:
    id_counter = 0
    def __init__(self, process=None):
        Job.id_counter += 1
        self.id = Job.id_counter
        self.process = process
        self.attached_terminals = []

    def on_stdout(self, data):
        for terminal in self.attached_terminals:
            terminal.write(data)

    def on_stderr(self, data):
        for terminal in self.attached_terminals:
            terminal.write(data)

    def write_stdin(self, data):
        if self.process:
            if self.process.get_returncode() is None:
                self.process.stdin.write(data)

    def on_job_done(self):
        for terminal in self.attached_terminals:
            terminal.on_job_done(self)
        self.attached_terminals = []

    def __str__(self):
        return repr(self.process.cmd_args)

class CoreEvent:
    pass

class CoreJobExitEvent(CoreEvent):
    def __init__(self, job):
        self.job = job
    

class ControllerCore:
    def __init__(self):
        self.terminals = set()
        self.jobs = []

    def start_picoprocess(self, image_path):
        job = Job()
        cwd = KVM_RUN_DIR + "/" + make_name(postfix=str(job.id))
        process = PicoProcess(image_path=image_path, cwd=cwd)
        job.process = process
        self._init_process(job)
        return job

    def resume_picoprocess(self, core_path):
        job = Job()
        cwd = KVM_RUN_DIR + "/" + make_name(postfix=str(job.id))
        os.makedirs(cwd)
        shutil.copy(core_path+"/kvm.core", cwd)
        shutil.copy(core_path+"/kvm.swap", cwd)
        process = PicoProcess(core_path=core_path, cwd=cwd)
        job.process = process
        self._init_process(job)
        return job

    def run_process(self, cmd_args):
        ''' For testing purpose. Disable this function in production.
        '''
        job = Job()
        process = NativeProcess(cmd_args)
        job.process = process
        self._init_process(job)
        return job

    def _init_process(self, job):
        process = job.process
        self.jobs.append(job)
        process.add_exit_callback(self.on_process_exit)
        process.tag = job
        process.execute()
        process.stdout.read_until_close(job.on_stdout, streaming_callback=job.on_stdout)
        process.stderr.read_until_close(job.on_stderr, streaming_callback=job.on_stderr)

    def on_process_exit(self, process):
        job = process.tag
        for terminal in self.terminals:
            terminal.on_core_event(CoreJobExitEvent(job))
        self.jobs.remove(job)
        job.on_job_done()

    def add_terminal(self, terminal):
        self.terminals.add(terminal)
        terminal.register(self)
        print "ControllerCore.add_terminal:", terminal.info()

    def remove_terminal(self, terminal):
        self.terminals.remove(terminal)
        print "ControllerCore.remove_terminal:", terminal.info()


class Terminal:
    def __init__(self):
        self.core = None
        self.attached_jobs = []

    def register(self, core):
        ''' Protected method. Invoke by ControllerCore. 
        '''
        self.core = core

    def disconnect(self):
        self.core.remove_terminal(self)
        for job in self.attached_jobs:
            job.attached_jobs.remove(self)

    def request(self, cmd, *args):
        args = list(args)
        if cmd == "run":
            job = self.core.run_process(args)
            job.attached_terminals.append(self)
            self.attached_jobs.append(job)
        elif cmd == "start":
            image_path = args[1]
            job = self.core.start_picoprocess(image_path)
            job.attached_terminals.append(self)
            self.attached_jobs.append(job)
        elif cmd == "resume":
            core_path = args[1]
            job = self.core.resume_picoprocess(core_path)
            job.attached_terminals.append(self)
            self.attached_jobs.append(job)
        elif cmd == "jobs":
            jobs = self.core.jobs
            self.write("Current jobs:\n")
            for job in jobs:
                self.write("{0}: {1}\n".format(job.id, job))


    def on_job_done(self, job):
        ''' Call when attached job done.
        '''
        self.attached_jobs.remove(job)

    def on_core_event(self, evt):
        if isinstance(evt, CoreJobExitEvent):
            job = evt.job
            self.write("JobExit: job.id={0}\n".format(job.id))

    def write(self, data):
        ''' Override this method to provide output to terminal. 
        '''
        pass

    def info(self):
        ''' Override this method to provide connected client info.
        '''
        return repr(self)


class TcpTerminal(Terminal):
    def __init__(self, stream, address):
        self.address = address
        self.stream = stream
        # stream.read_until(b'\n', self.on_request)
        stream.read_until_close(self.on_request, self.on_request)
        stream.set_close_callback(self.on_close)
        Terminal.__init__(self)

        # cmd_args = ['bash']
        # _process = Subprocess(cmd_args, stdin=Subprocess.STREAM, stdout=Subprocess.STREAM, stderr=Subprocess.STREAM)
        # _process.set_exit_callback(lambda returncode: self.write("Process {0} return {1}".format(_process.pid, returncode)))
        # self._process = _process
        # self._process.stdout.read_until_close(lambda data: self.write(data), streaming_callback=lambda data: self.write(data))
        # self._process.stderr.read_until_close(lambda data: self.write(data), streaming_callback=lambda data: self.write(data))

    def on_close(self):
        # self._process.kill()
        self.disconnect()

    def on_request(self, data):
        print "on_request", repr(data)
        cmd_args = data.split()
        if len(cmd_args) > 0:
            self.request(cmd_args[0], *cmd_args[1:])
        
    def write(self, data):
        if not self.stream.closed():
            self.stream.write(data)

    def info(self):
        return "TcpTerminal(address={0})".format(self.address)


class TcpHub(tornado.tcpserver.TCPServer):
    def __init__(self, core):
        self.core = core
        super(TcpHub, self).__init__()

    def handle_stream(self, stream, address):
        terminal = TcpTerminal(stream, address)
        self.core.add_terminal(terminal)

# class PicoHandler(tornado.web.RequestHandler):
#     def get(self):
#         self.write("Hello world")

def start_tornado(port=WEB_PORT):
    # app = tornado.web.Application([(r"/", PicoHandler)])
    # app.listen(port)
    Subprocess.initialize() # init SIGCHID handler
    core = ControllerCore()
    hub = TcpHub(core)
    hub.listen(port)
    tornado.ioloop.IOLoop.instance().start()

def main():
    start_tornado()

if __name__ == '__main__':
    main()
