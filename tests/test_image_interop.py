"""Real C++/Python TCP integration + adversarial wire tests. No UI automation."""
import socket
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'python'))
from PIL import Image
from protocol import Frame, Decoder, encode, load_image, image_info, to_image, META, HEADER

class ProtocolTests(unittest.TestCase):
    def test_splits_and_batch(self):
        frame=Frame(5,42,META.pack(2,1,3,8,6,6)+bytes(range(6)))
        raw=encode(frame)
        for split in range(len(raw)+1):
            d=Decoder(); self.assertEqual(d.feed(raw[:split])+d.feed(raw[split:]),[frame])
        self.assertEqual(Decoder().feed(raw*3),[frame]*3)

    def test_invalid_metadata(self):
        for meta in [(0,1,1,8,0,0),(1,1,2,8,2,2),(1,1,3,16,3,3),
                     (1,1,3,8,4,4),(1,2,1,8,1,1),(16385,1,1,8,16385,16385)]:
            with self.assertRaises(ValueError):
                image_info(META.pack(*meta)+bytes(meta[-1]))
        with self.assertRaises(ValueError):
            Decoder().feed(HEADER.pack(b'TCPT',1,5,1,64*1024*1024+25))
        with self.assertRaises(ValueError):
            Decoder().feed(HEADER.pack(b'TCPT',1,1,1,2)+b'\xc0\x80')

def integration(executable):
    root=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='image-interop-') as name:
        directory=Path(name); files=[root/'000028.JPG']
        for mode,size in [('L',(37,19)),('RGB',(123,67)),('RGBA',(31,43)),('L',(1,1)),('1',(11,7))]:
            image=Image.new(mode,size)
            if mode=='1': image.putpixel((0,0),1)
            else:
                channels=len(mode) if mode!='L' else 1
                image=Image.frombytes(mode,size,bytes((i*17+23)%256 for i in range(size[0]*size[1]*channels)))
            path=directory/f'{mode}_{size[0]}.png'; image.save(path); files.append(path)
        gray_jpeg=directory/'gray.jpg'; Image.new('L',(47,23),127).save(gray_jpeg); files.append(gray_jpeg)
        proc=subprocess.Popen([str(executable),str(directory),*map(str,files)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            port=int(proc.stdout.readline().strip())
            with socket.create_connection(('127.0.0.1',port),timeout=20) as sock:
                sock.settimeout(20); decoder=Decoder(); initial=[]
                while len(initial)<len(files):
                    chunk=sock.recv(65536)
                    if not chunk: raise AssertionError('premature EOF')
                    initial.extend(decoder.feed(chunk))
                for frame,path in zip(initial,files):
                    expected=load_image(path)
                    assert frame.kind==5 and image_info(frame.payload)==image_info(expected),(path,image_info(frame.payload),image_info(expected))
                    if path.suffix.lower()=='.png': assert frame.payload==expected,path
                    # JPEG decoders can differ slightly: compare metadata, then exact wire roundtrip below.
                    sock.sendall(encode(Frame(4,frame.sequence,b'')))
                for seq,path in enumerate(files,100):
                    payload=load_image(path); raw=encode(Frame(5,seq,payload))
                    # Force header and image metadata across separate writes.
                    for b in raw[:40]: sock.sendall(bytes([b]))
                    sock.sendall(raw[40:])
                    responses=[]
                    while len(responses)<2:
                        chunk=sock.recv(65536)
                        if not chunk: raise AssertionError('premature EOF')
                        responses.extend(decoder.feed(chunk))
                    assert responses==[Frame(5,seq,payload),Frame(4,seq,b'')]
                    saved=load_image(directory/f'{seq}.png')
                    assert saved==payload, 'C++ PNG save changed pixels or channels'
                    print(f'PASS Python -> C++ -> Python exact bytes + PNG: {path.name} {image_info(payload)}')
                # Two differently sized images coalesced, then graceful half-close.
                batch=[Frame(5,200+i,load_image(p)) for i,p in enumerate(files[1:3])]
                sock.sendall(b''.join(map(encode,batch)))
                sock.shutdown(socket.SHUT_WR)
                responses=[]
                while chunk:=sock.recv(65536): responses.extend(decoder.feed(chunk))
                assert responses==[batch[0],Frame(4,200,b''),batch[1],Frame(4,201,b'')]
                assert not decoder.buffer
            _,err=proc.communicate(timeout=10)
            assert proc.returncode==0,err
            print('PASS C++ -> Python file decode; dynamic channels/resolution, batch and EOF')
        finally:
            if proc.poll() is None: proc.kill()
            _,diagnostics=proc.communicate()
            if diagnostics: print(diagnostics,file=sys.stderr)

if __name__=='__main__':
    result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(ProtocolTests))
    if not result.wasSuccessful(): sys.exit(1)
    if len(sys.argv)>1: integration(Path(sys.argv[1]).resolve())
