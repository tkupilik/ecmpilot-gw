"""Actual C GVRET service over real loopback TCP; IDF queue/task/time are host boundaries."""
import csv
import ctypes as c
from pathlib import Path
import socket
import struct
import unittest

ROOT=Path(__file__).resolve().parents[1]
LIB=c.CDLL(str(ROOT/'build-host-gvret/gvret-test.dll'))
HOOK=c.CFUNCTYPE(c.c_int,c.c_int,c.c_int,c.c_void_p,c.c_int,c.c_int)
LIB.gvret_test_hook.argtypes=[HOOK]
LIB.gvret_test_poll.argtypes=[c.c_uint64]
LIB.gvret_test_stat.argtypes=[c.c_char_p]
LIB.gw_gvret_capture.argtypes=[c.c_uint32,c.c_void_p,c.c_size_t,c.c_bool,c.c_bool,c.c_uint64]
LIB.gvret_test_config.argtypes=[c.c_uint]
LIB.gvret_test_token.restype=c.c_uint

def decode(data):
    """Independent GVRET consumer: controls and CAN records can share a TCP read."""
    items=[];offset=0
    sizes={1:6,6:12,7:8,9:4,12:3,13:17}
    while offset<len(data):
        if data[offset]!=0xF1: raise AssertionError(f'Bad marker at {offset}')
        cmd=data[offset+1]
        length=12+(data[offset+10]&15) if cmd==0 else sizes[cmd]
        record=data[offset:offset+length]
        if len(record)!=length: raise AssertionError('Truncated record')
        if cmd==0:
            timestamp,ident=struct.unpack_from('<II',record,2)
            n=record[10]&15
            if record[-1]!=0: raise AssertionError('Bad legacy trailer')
            items.append((0,timestamp,ident,record[10]>>4,record[11:11+n]))
        else: items.append((cmd,record))
        offset+=length
    return items

class GvretTests(unittest.TestCase):
    def setUp(self):
        self.files={};self.next_fd=100;self.port=None;self.now=1000000
        self.block_send=False;self.send_limit=65536;self.hook_errors=[];self.clients=[]
        self.accept_error=0
        self.hook=HOOK(self.io);LIB.gvret_test_hook(self.hook);LIB.gvret_test_reset()
        self.assertEqual(0,LIB.gw_gvret_init());self.assertEqual(0,LIB.gw_gvret_start())

    def tearDown(self):
        LIB.gvret_test_reset()
        for sock in self.clients+list(self.files.values()): sock.close()
        self.assertEqual([],self.hook_errors)

    def register(self,sock):
        fd=self.next_fd;self.next_fd+=1;self.files[fd]=sock;return fd

    def io(self,op,fd,pointer,length,arg):
        try:
            if op==1: return self.register(socket.socket(socket.AF_INET,socket.SOCK_STREAM))
            sock=self.files[fd]
            if op==2:
                raw=c.string_at(pointer,length)
                if int.from_bytes(raw[2:4],'big')!=23: raise AssertionError('Unexpected GVRET port')
                sock.bind(('127.0.0.1',0));self.port=sock.getsockname()[1];return 0
            if op==3: sock.listen(length);return 0
            if op==4:
                if self.accept_error:return -self.accept_error
                return self.register(sock.accept()[0])
            if op==5:
                if self.block_send:return -11
                return sock.send(c.string_at(pointer,min(length,self.send_limit)))
            if op==6:
                data=sock.recv(length);c.memmove(pointer,data,len(data));return len(data)
            if op==7: sock.close();del self.files[fd];return 0
            if op==8:
                if length==6 and arg==1: sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
                elif length==1 and arg==2: sock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
                elif length==1 and arg==9: sock.setsockopt(socket.SOL_SOCKET,socket.SO_KEEPALIVE,1)
                return 0
            if op==9:return 0
            if op==10:sock.setblocking(False);return 0
            raise AssertionError(f'Unknown IO operation {op}')
        except BlockingIOError:return -11
        except (ConnectionResetError,BrokenPipeError):return -104
        except Exception as exc:
            self.hook_errors.append(repr(exc));return -104

    def poll(self,count=1):
        for _ in range(count):self.now+=1000;LIB.gvret_test_poll(self.now)

    def stat(self,key):return LIB.gvret_test_stat(('gvret.'+key.lower()).encode())

    def connect(self,binary=True):
        sock=socket.create_connection(('127.0.0.1',self.port),timeout=2)
        sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);sock.setblocking(False)
        self.clients.append(sock);self.poll()
        if binary:sock.sendall(b'\xe7\xe7');self.poll()
        return sock

    def read(self,sock):
        result=bytearray()
        while True:
            try:
                data=sock.recv(65536)
                if not data:break
                result.extend(data)
            except BlockingIOError:break
        return bytes(result)

    def capture(self,ident=0x128,payload=b'\x01\xf1\xe7\x00',timestamp=123456,ext=False,rtr=False):
        LIB.gw_gvret_capture(ident,payload,len(payload),ext,rtr,timestamp)

    def test_task_fifo_memory_and_no_history_without_binary_client(self):
        self.assertEqual([1,0,3,6144,24],[LIB.gvret_test_config(i) for i in range(5)])
        self.assertEqual(1536,LIB.gvret_test_config(6));self.assertLess(LIB.gvret_test_config(5),4096)
        self.capture();self.poll();self.assertEqual(0,self.stat('ENQUEUED'))
        sock=self.connect(binary=False);self.capture();self.poll()
        self.assertEqual(0,self.stat('ENQUEUED'));self.assertEqual(b'',self.read(sock))

    def test_savvy_handshake_fragmented_bytewise(self):
        sock=self.connect(False)
        for byte in b'\xe7\xe7\xf1\x0c\xf1\x06\xf1\x07\xf1\x01\xf1\x09':
            sock.sendall(bytes([byte]));self.poll()
        records=decode(self.read(sock));self.assertEqual([12,6,7,1,9],[r[0] for r in records])
        self.assertEqual(b'\xf1\x0c\x01',records[0][1])
        self.assertEqual(0x11,records[1][1][2]);self.assertEqual(125000,struct.unpack_from('<I',records[1][1],3)[0])
        self.assertEqual(bytes(5),records[1][1][7:]);self.assertEqual(b'\xf1\x09\xde\xad',records[-1][1])
        sock.sendall(b'\xf1\x0d');self.poll();self.assertEqual(b'\xf1\x0d'+bytes(15),self.read(sock))
        self.assertEqual(3,self.stat('STATE'))

    def test_standard_extended_raw_ids_payload_and_receive_timestamp(self):
        sock=self.connect();self.capture(0x777,b'\xf1\x00\xe7',123)
        self.capture(0x1ABCDE,b'\x01\x02',456,ext=True);self.now+=500000;self.poll()
        self.assertEqual([(0,123,0x777,0,b'\xf1\x00\xe7'),(0,456,0x801ABCDE,0,b'\x01\x02')],decode(self.read(sock)))
        self.assertEqual(2,self.stat('SENT'))

    def test_rtr_and_invalid_frames_are_ignored(self):
        sock=self.connect()
        self.capture(rtr=True);self.capture(payload=bytes(9));self.capture(0x800);self.capture(0x20000000,ext=True)
        self.poll();self.assertEqual(b'',self.read(sock));self.assertEqual(0,self.stat('ENQUEUED'))
        self.capture(payload=b'');self.poll();self.assertEqual([(0,123456,0x128,0,b'')],decode(self.read(sock)))

    def test_full_fifo_never_blocks_and_counts_new_frame_losses(self):
        sock=self.connect()
        for i in range(81):self.capture(timestamp=i)
        self.poll();self.assertEqual(64,self.stat('ENQUEUED'));self.assertEqual(17,self.stat('DROPPED'))
        self.assertEqual(64,self.stat('QUEUE_PEAK'))
        wire=bytearray(self.read(sock))
        for _ in range(40):self.poll();wire.extend(self.read(sock))
        self.assertEqual(list(range(64)),[r[1] for r in decode(wire)])
        self.assertEqual(64,self.stat('SENT'))

    def test_partial_send_keeps_exact_frame_boundaries(self):
        sock=self.connect();self.send_limit=3
        for i in range(3):self.capture(timestamp=i)
        self.poll();self.assertEqual(0,self.stat('SENT'))
        wire=bytearray(self.read(sock))
        for _ in range(30):self.poll();wire.extend(self.read(sock))
        self.assertEqual([0,1,2],[r[1] for r in decode(wire)]);self.assertEqual(3,self.stat('SENT'))

    def test_second_connect_is_closed_and_first_keeps_working(self):
        first=self.connect();second=self.connect(False);self.poll()
        self.assertEqual(b'',second.recv(1));self.assertEqual(1,self.stat('REJECTED_CLIENTS'))
        first.sendall(b'\xf1\x09');self.poll();self.assertEqual(b'\xf1\x09\xde\xad',self.read(first))
        self.assertEqual(1,LIB.gvret_test_config(0)) # No new task/session thread.

    def test_disconnect_clears_pending_output_and_fifo(self):
        first=self.connect();self.block_send=True
        for i in range(64):self.capture(timestamp=i)
        self.poll();first.close();self.poll();self.assertEqual(1,self.stat('STATE'))
        self.assertEqual(0,self.stat('QUEUED'));self.assertEqual(64,self.stat('DISCARDED'))
        self.block_send=False;second=self.connect();self.capture(timestamp=999);self.poll()
        self.assertEqual([999],[r[1] for r in decode(self.read(second))])

    def test_inflight_old_producer_cannot_leak_to_new_session(self):
        first=self.connect();old=LIB.gvret_test_token();first.close();self.poll()
        second=self.connect();self.assertNotEqual(old,LIB.gvret_test_token())
        LIB.gvret_test_stale(old);self.capture(timestamp=888);self.poll()
        self.assertEqual([888],[r[1] for r in decode(self.read(second))]);self.assertEqual(1,self.stat('DISCARDED'))

    def test_slow_client_is_disconnected_without_delaying_can_producer(self):
        first=self.connect();self.block_send=True;self.capture();self.poll()
        self.now+=2000001;self.poll();self.assertEqual(1,self.stat('STATE'));self.assertEqual(110,self.stat('LAST_ERROR'))
        self.block_send=False;second=self.connect();second.sendall(b'\xf1\x09');self.poll()
        self.assertEqual(b'\xf1\x09\xde\xad',self.read(second))

    def test_handshake_timeout_releases_slot(self):
        self.connect(False);self.now+=5000001;self.poll()
        self.assertEqual(1,self.stat('STATE'));self.assertEqual(0,LIB.gvret_test_token())
        self.connect();self.assertEqual(3,self.stat('STATE'))

    def test_partial_command_timeout_releases_slot(self):
        sock=self.connect();sock.sendall(b'\xf1\x05\x01');self.poll();self.now+=1000001;self.poll()
        self.assertEqual(1,self.stat('STATE'));self.assertEqual(110,self.stat('LAST_ERROR'))

    def test_can_tx_and_configuration_commands_are_consumed_only(self):
        sock=self.connect()
        tx=b'\xf1\x00'+struct.pack('<I',0x123)+bytes([0,4])+b'\xf1\xe7\xf1\x09'+b'\x00'
        config=b'\xf1\x05'+struct.pack('<II',0xC0000000|500000,0)+b'\x00'
        sock.sendall(tx+config+b'\xf1\x06\xf1\x09');self.poll()
        records=decode(self.read(sock));self.assertEqual([6,9],[r[0] for r in records])
        self.assertEqual(125000,struct.unpack_from('<I',records[0][1],3)[0]);self.assertEqual(2,self.stat('IGNORED_COMMANDS'))
        self.assertEqual(0,self.stat('ENQUEUED'))

    def test_unknown_command_closes_cleanly_and_reconnects(self):
        sock=self.connect();sock.sendall(b'\xf1\xff');self.poll()
        self.assertEqual(1,self.stat('PROTOCOL_ERRORS'));self.assertEqual(1,self.stat('STATE'))
        self.connect();self.assertEqual(3,self.stat('STATE'))

    def test_exhausted_socket_table_is_reported_and_accept_recovers_without_restart(self):
        self.accept_error=23;self.poll()
        self.assertEqual(1,self.stat('STATE'));self.assertEqual(23,self.stat('LAST_ERROR'))
        self.accept_error=0;sock=self.connect();self.poll()
        self.assertEqual(3,self.stat('STATE'));self.assertEqual(0,self.stat('LAST_ERROR'))
        self.capture();self.poll();self.assertEqual(1,len(decode(self.read(sock))))

    def test_timestamp_low32_wrap_and_time_sync_use_same_clock(self):
        sock=self.connect();self.now=(1<<32)+1234
        self.capture(timestamp=(1<<32)+17);sock.sendall(b'\xf1\x01');self.poll()
        records=decode(self.read(sock));self.assertEqual([1,0],[r[0] for r in records])
        self.assertEqual(self.now&0xFFFFFFFF,struct.unpack_from('<I',records[0][1],2)[0]);self.assertEqual(17,records[1][1])

    def test_controls_remain_responsive_while_frames_stream(self):
        sock=self.connect()
        for i in range(40):self.capture(timestamp=i)
        sock.sendall(b'\xf1\x09');self.poll();wire=bytearray(self.read(sock))
        for _ in range(5):self.poll();wire.extend(self.read(sock))
        records=decode(wire);self.assertEqual(9,records[0][0]);self.assertEqual(40,len(records)-1)

    def test_full_owner_capture_exports_all_ids_in_original_order(self):
        allocation_before=LIB.gvret_test_config(5)
        sock=self.connect();rows=list(csv.DictReader((ROOT/'logs/info_start_jizda.csv').read_text().splitlines()))
        origin=int(rows[0]['Time Stamp']);expected=[];wire=bytearray()
        for i,row in enumerate(rows):
            ident=int(row['ID'],16);stamp=int(row['Time Stamp'])-origin
            payload=bytes(int(row[f'D{j}'],16) for j in range(1,int(row['LEN'])+1))
            self.capture(ident,payload,stamp);expected.append((0,stamp,ident,0,payload))
            if i%64==63:self.poll(2);wire.extend(self.read(sock))
        for _ in range(40):self.poll();wire.extend(self.read(sock))
        self.assertEqual(9520,len(expected));self.assertEqual(expected,decode(wire))
        self.assertEqual(0,self.stat('DROPPED'));self.assertEqual(9520,self.stat('SENT'))
        self.assertEqual(allocation_before,LIB.gvret_test_config(5))

if __name__=='__main__':unittest.main()
