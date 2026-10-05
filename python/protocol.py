"""TCPT v1: C++ protocol.h and this module share an explicit wire contract."""
from dataclasses import dataclass
import struct
from PIL import Image

HEADER = struct.Struct('!4sHHII')
META = struct.Struct('!6I')
MAX_IMAGE = 64 * 1024 * 1024
MODES = {1: 'L', 3: 'RGB', 4: 'RGBA'}

@dataclass
class Frame:
    kind: int
    sequence: int
    payload: bytes

def validate_header(kind, size):
    if kind not in range(1, 6):
        raise ValueError('unknown message type')
    if size > (MAX_IMAGE + META.size if kind == 5 else 65536):
        raise ValueError('payload exceeds limit')
    if kind in (2, 3, 4) and size != {2: 4, 3: 8, 4: 0}[kind]:
        raise ValueError('payload length does not match type')
    if kind == 5 and size < META.size:
        raise ValueError('missing image metadata')

def image_info(payload):
    if len(payload) < META.size:
        raise ValueError('missing image metadata')
    w, h, c, bits, stride, size = META.unpack_from(payload)
    if not (0 < w <= 16384 and 0 < h <= 16384 and c in MODES and bits == 8
            and stride == w*c and size == stride*h and size <= MAX_IMAGE
            and len(payload) == META.size+size):
        raise ValueError('invalid image dimensions/channels/bits/stride/size')
    return w, h, c, bits, stride, size

def validate(frame):
    validate_header(frame.kind, len(frame.payload))
    if frame.kind == 1:
        frame.payload.decode('utf-8', errors='strict')
    if frame.kind == 5:
        image_info(frame.payload)

def encode(frame):
    validate(frame)
    return HEADER.pack(b'TCPT', 1, frame.kind, frame.sequence, len(frame.payload)) + frame.payload

class Decoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        offset = 0
        while len(self.buffer)-offset >= HEADER.size:
            magic, version, kind, seq, size = HEADER.unpack_from(self.buffer, offset)
            if magic != b'TCPT' or version != 1:
                raise ValueError('bad magic or unsupported version')
            validate_header(kind, size)
            if len(self.buffer)-offset < HEADER.size+size:
                break
            f = Frame(kind, seq, bytes(self.buffer[offset+HEADER.size:offset+HEADER.size+size]))
            validate(f)
            frames.append(f)
            offset += HEADER.size+size
        del self.buffer[:offset]
        return frames

def load_image(path):
    with Image.open(path) as source:
        w, h = source.size
        if source.mode in ('I', 'F') or source.mode.startswith('I;16'):
            raise ValueError('Only 8-bit/channel input supported; convert high-depth images explicitly')
        mode = 'L' if source.mode in ('1', 'L') else (
            'RGBA' if 'A' in source.getbands() or 'transparency' in source.info else 'RGB')
        c = len(mode) if mode != 'L' else 1
        if not (0 < w <= 16384 and 0 < h <= 16384 and w*h*c <= MAX_IMAGE):
            raise ValueError('image exceeds dimension or 64 MiB limit')
        image = source.convert(mode)
        pixels = image.tobytes()
        return META.pack(w, h, c, 8, w*c, len(pixels)) + pixels

def to_image(payload):
    w, h, c, _, _, _ = image_info(payload)
    return Image.frombytes(MODES[c], (w, h), payload[META.size:])

def describe(frame):
    prefix = f'seq={frame.sequence} type={frame.kind} length={len(frame.payload)}'
    if frame.kind == 5:
        w,h,c,bits,stride,size = image_info(frame.payload)
        return f'{prefix} IMAGE {w}x{h} channels={c} {MODES[c]} bits={bits} stride={stride} pixels={size} B'
    if frame.kind == 1:
        value = repr(frame.payload.decode('utf-8'))
    elif frame.kind in (2,3):
        value = struct.unpack('!i' if frame.kind == 2 else '!d', frame.payload)[0]
    else:
        value = 'ACK (peer parsed frame)'
    return f'{prefix} value={value}'
