"""Private wire 0.2 transport. Only the reader thread touches blocking reads."""
import ctypes
import json
import struct
import threading
import win32file
import win32event
import pywintypes
from . import artest_process_pb2 as wire

MAX_FRAME = 1024 * 1024

def json_load(text):
    return json.loads(text or "null", parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))

class Transport:
    def __init__(self, bootstrap_handle):
        _, raw = win32file.ReadFile(bootstrap_handle, 4096)
        win32file.CloseHandle(bootstrap_handle)
        self.bootstrap = json_load(raw)
        # A synchronous Windows handle serializes read/write I/O. The control reader
        # would then block responses on the same duplex pipe. Separate OVERLAPPED
        # operations preserve full-duplex progress without polling or shared buffers.
        self.pipe = win32file.CreateFile(self.bootstrap["pipe"], 0xC0000000, 0, None, 3, 0x40000000, None)
        pid = ctypes.c_ulong()
        if not ctypes.windll.kernel32.GetNamedPipeServerProcessId(int(self.pipe), ctypes.byref(pid)):
            raise OSError("Cannot identify Engine pipe server")
        if pid.value != self.bootstrap["parentPid"]: raise ValueError("Unexpected Engine process")
        self.generation = self.bootstrap["generation"]
        self.send_lock = threading.Lock()

    def envelope(self, correlation, parent=0):
        return wire.Envelope(major=0, minor=2, generation=self.generation, correlation=correlation, parent=parent)

    def send(self, message):
        data = message.SerializeToString()
        if not 0 < len(data) <= MAX_FRAME: raise ValueError("Frame exceeds control-plane limit")
        with self.send_lock:
            operation = pywintypes.OVERLAPPED()
            operation.hEvent = win32event.CreateEvent(None, True, False, None)
            block = struct.pack("<I", len(data)) + data
            win32file.WriteFile(self.pipe, block, operation)
            count = win32file.GetOverlappedResult(self.pipe, operation, True)
            if count != len(block): raise OSError("Partial pipe write")

    def _read(self, count):
        data = bytearray()
        while len(data) < count:
            operation = pywintypes.OVERLAPPED()
            operation.hEvent = win32event.CreateEvent(None, True, False, None)
            block = win32file.AllocateReadBuffer(count - len(data))
            win32file.ReadFile(self.pipe, block, operation)
            received = win32file.GetOverlappedResult(self.pipe, operation, True)
            if not received: raise EOFError("Engine pipe closed")
            data.extend(block[:received])
        return bytes(data)

    def receive(self):
        size, = struct.unpack("<I", self._read(4))
        if not 0 < size <= MAX_FRAME: raise ValueError("Invalid frame size")
        message = wire.Envelope.FromString(self._read(size))
        if (message.major, message.minor, message.generation) != (0, 2, self.generation) or not message.correlation:
            raise ValueError("Invalid wire identity/version")
        if message.WhichOneof("body") is None: raise ValueError("Missing message body")
        return message

    def handshake(self):
        message = self.envelope(1)
        message.hello.extension_id = self.bootstrap["extension"]
        message.hello.fingerprint = self.bootstrap["fingerprint"]
        message.hello.nonce = bytes.fromhex(self.bootstrap["nonce"])
        self.send(message)
        reply = self.receive()
        message.hello.acknowledged = True
        if reply != message: raise ValueError("Invalid handshake acknowledgement")
