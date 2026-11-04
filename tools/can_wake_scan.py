#!/usr/bin/env python3
"""Try one recorded payload per CAN ID, resetting PCAN after every bounded trial.

Default is offline preview. --execute opens real PCAN hardware. Standard library
only; Windows execution requires an x64 PCANBasic.dll (API >= 4.8).
"""
import argparse
from collections import Counter
import ctypes as ct
import ctypes.util
from dataclasses import dataclass
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import time

# First actual Rx payload for each standard ID in logs/opel_apka.csv.
# Preserve observed DLC; these are samples, NOT validated vehicle commands.
APP_FRAMES = (
    (0x102, '99 1F 07 23 01 12 B2 01'),
    (0x112, '00 00 00 00 00 01 00 00'),
    (0x12A, '74 09 02 15 05 04 0E 01'),
    (0x208, 'FF FF 51 00 2C 00 00 32'),
    (0x262, 'E0'),
    (0x2ED, '11 00 00 00 00 00 00'),
    (0x30D, '00 00 00 00 00 00 00 00'),
    (0x334, '48 00 00 00 00 00 00 00'),
    (0x348, '00 00 FF 00 C1 21 00 11'),
    (0x349, '03 55 00 10 00 00 51 20'),
    (0x34D, '10 23 64 00 09 01 00 3E'),
    (0x352, '00 FD'),
    (0x36D, '0F B0 00 00'),
    (0x38D, '00 00 00 00 AF 14 45 00'),
    (0x3A8, '06 06 FE 00 00 00'),
    (0x3AD, '0F FF 80 00 80 00 00 1F'),
    (0x3CD, '00 00 00 00 03 22 70 32'),
    (0x3CE, '40 62 00 00 02 24 00 00'),
    (0x3EE, '00 00 00 00'),
    (0x3F2, '00 81 00 00 00 00 00 02'),
    (0x3F4, '80 28 28 28 28 28 28 28'),
    (0x40D, '00 00 00 00 00 00 00 02'),
    (0x40E, '0D 0A 2A 28 00'),
    (0x412, '10 00 00 00 00 3C 00 00'),
    (0x432, '42 19 40 FE 00 08 30 02'),
    (0x44D, '00 00 00 00 00 00 00 00'),
    (0x44E, '35 C0 00'),
    (0x454, '0C 40 AB 76 C0 00 20 20'),
    (0x462, '40 00 00 00'),
    (0x468, '00 30 40 C0 00 00 00 00'),
    (0x46E, '00 00'),
    (0x489, '00 01 10 28 00 01 00 3F'),
    (0x494, '0F E0 00 17 60 6C 00 00'),
    (0x4A2, '00 80'),
    (0x4CE, '00 00 00 00 00 00 00 00'),
    (0x4E9, '00 17 FF FF'),
    (0x4F8, '00 00 31 00 00'),
    (0x4FE, 'A0 C0 08'),
    (0x50D, 'C1 00 00 00 00 73 8E 1E'),
    (0x512, '07 10 00 00'),
    (0x518, '40 00 00 00'),
    (0x522, '00 00 00 00 00 00 00 00'),
    (0x52E, 'FF FF 00 00 00 00 00 00'),
    (0x543, '00 01 00 00'),
    (0x54E, '00 00 00 00 00 00 00 00'),
    (0x552, '05 76 20 35 00 E6 70 FE'),
    (0x563, '00 00 00 00 00 00 00'),
    (0x56B, '80 00 FF 40'),
    (0x56E, 'F4 00 00 00 00 00'),
    (0x572, '50 80 FF C0 00 00 00 00'),
    (0x583, '00 00 00 20 00 00 00'),
    (0x588, 'FE FF 00 00 00 54 20 00'),
    (0x58E, '00 00 00 00 00 00 00 00'),
    (0x598, '00 00 00 00'),
    (0x5A2, '00 00 00 00 00 00 00'),
    (0x5A3, '00 00 00 00 00 00 00 00'),
    (0x5AE, '00 00 00 00 00'),
    (0x5B2, '00 00 62 80 00 02 00 00'),
    (0x5C1, '00 00 00 00 00 00 00 00'),
    (0x5C3, '00 00 00 00 00 00 00 00'),
    (0x5CD, 'E1 7F FE F0'),
    (0x5CE, '00 00 00'),
    (0x5E2, '00 00 00 00 00 00 00 00'),
    (0x5EB, '00 00 00 00 00 60 00 00'),
    (0x612, '00 20 01 00 07 FE 00 10'),
    (0x782, '00 00 00 00 00 00 00 00'),
    (0x78D, '28 00 00 00 00 00 00 00'),
    (0x792, '00 00 00 00 00 10 00 00'),
)
# IDs only present in the READY capture, logs/obd_hscan1.csv.
READY_EXTRA_FRAMES = (
    (0x0A6, '02 00'), (0x25A, '80 B6'),
    (0x2F5, '1B 00 00 FF D2 FF D7'), (0x305, 'FF E8 01 07 CC 01 30'),
    (0x3FA, '3A 32'), (0x40B, '7F 9F E0 00'),
    (0x41E, '00 00 30 00 03 00 C0 10'),
    (0x42B, '7F FE 7F FE 7F E0 7F 80'), (0x43E, 'FE FE FE FE FE FE'),
    (0x44B, '7F FE 7F FE 7F E0 7F 80'), (0x45E, 'FE FE FE FE FE FE'),
    (0x47E, 'FE FE FE F0 EA EA EA 00'), (0x495, '00 02 22 3C'),
    (0x4FA, '11 00 00 00'), (0x53A, 'B8 B6 00'),
    (0x55A, '80 7E 34 7E 34 00 00'), (0x596, 'FE EF EF 20'),
    (0x5CB, '01 00 00 00 00 40 00 00'), (0x5F2, '00 00 FE C0 FE'),
    (0x795, '00 00 00 00 00 90'), (0x79A, 'B8 00 00 00 00 00 00 00'),
)


@dataclass(frozen=True)
class Frame:
    ident: int
    data: bytes


def frames_for(profile):
    values = APP_FRAMES + (READY_EXTRA_FRAMES if profile == 'all' else ())
    frames = [Frame(ident, bytes.fromhex(data)) for ident, data in sorted(values)]
    if len({f.ident for f in frames}) != len(frames):
        raise ValueError('Duplicate CAN ID')
    if any(not 0 <= f.ident <= 0x7FF or len(f.data) > 8 for f in frames):
        raise ValueError('Only standard Classic CAN frames are supported')
    return frames


class Msg(ct.Structure):
    _fields_ = [('ident', ct.c_uint32), ('kind', ct.c_uint8), ('length', ct.c_uint8),
                ('data', ct.c_uint8 * 8)]


class Timestamp(ct.Structure):
    _fields_ = [('millis', ct.c_uint32), ('overflow', ct.c_uint16), ('micros', ct.c_uint16)]


class Journal:
    def __init__(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.stream = path.open('x', encoding='utf-8')
        self.start = time.perf_counter()

    def __call__(self, event, **values):
        row = {'t_s': round(time.perf_counter() - self.start, 6), 'event': event, **values}
        self.stream.write(json.dumps(row) + '\n')
        self.stream.flush()
        if event != 'rx':
            print(json.dumps(row), flush=True)

    def close(self):
        self.stream.close()


class Pcan:
    """Small PCAN-Basic binding. No SDK Python package or global installation."""
    def __init__(self, dll, channel, emit):
        if os.name != 'nt':
            raise RuntimeError('Real PCAN execution requires Windows')
        self.api = ct.WinDLL(str(Path(dll).resolve()))
        self.channel, self.emit = channel, emit
        self.opened = False
        self.hard_enabled = False
        # Signatures from PCAN-Basic.h. Windows DWORD is always 32 bits.
        signatures = {
            'CAN_Initialize': [ct.c_uint16, ct.c_uint16, ct.c_uint8, ct.c_uint32, ct.c_uint16],
            'CAN_Uninitialize': [ct.c_uint16], 'CAN_Reset': [ct.c_uint16],
            'CAN_GetStatus': [ct.c_uint16],
            'CAN_SetValue': [ct.c_uint16, ct.c_uint8, ct.c_void_p, ct.c_uint32],
            'CAN_GetValue': [ct.c_uint16, ct.c_uint8, ct.c_void_p, ct.c_uint32],
            'CAN_Write': [ct.c_uint16, ct.POINTER(Msg)],
            'CAN_Read': [ct.c_uint16, ct.POINTER(Msg), ct.POINTER(Timestamp)],
        }
        for name, args in signatures.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = args, ct.c_uint32

    @staticmethod
    def checked(status, action):
        if status:
            raise RuntimeError(f'{action}: PCAN status 0x{status:X}')

    def set_value(self, parameter, value):
        buffer = ct.c_uint32(value)
        self.checked(self.api.CAN_SetValue(self.channel, parameter, ct.byref(buffer), 4),
                     f'SetValue({parameter})')

    def get_value(self, parameter):
        buffer = ct.c_uint32()
        self.checked(self.api.CAN_GetValue(self.channel, parameter, ct.byref(buffer), 4),
                     f'GetValue({parameter})')
        return buffer.value

    def open(self):
        condition = self.get_value(0x0D)  # CHANNEL_CONDITION, readable before init.
        if not condition & 1:
            raise RuntimeError(f'PCAN channel unavailable or owned by another API client '
                               f'(condition 0x{condition:X}); disconnect that client first')
        self.checked(self.api.CAN_Initialize(self.channel, 0x001C, 0, 0, 0), 'Initialize 500k')
        self.opened = True
        # 46 = HARD_RESET_STATUS; 44 = ALLOW_ECHO_FRAMES; 4 = MESSAGE_FILTER.
        self.set_value(46, 1)
        if self.get_value(46) != 1:
            raise RuntimeError('Hard reset could not be verified; no transmission permitted')
        self.hard_enabled = True
        self.set_value(44, 1)
        self.set_value(4, 1)  # Open receive filter.
        if self.get_value(8) != 0:  # LISTEN_ONLY
            raise RuntimeError('Channel is listen-only; no transmission permitted')
        self.emit('connected', channel=f'0x{self.channel:X}', bitrate=500000, hard_reset=True)

    def reset(self):
        if not self.hard_enabled:
            raise RuntimeError('Refusing queue-only reset')
        self.checked(self.api.CAN_Reset(self.channel), 'Hardware reset')
        self.emit('hard_reset', status=f'0x{self.api.CAN_GetStatus(self.channel):X}')

    def write(self, frame):
        if not self.hard_enabled:
            raise RuntimeError('Hard reset not configured')
        msg = Msg(ident=frame.ident, kind=0, length=len(frame.data))
        for index, value in enumerate(frame.data):
            msg.data[index] = value
        self.checked(self.api.CAN_Write(self.channel, ct.byref(msg)), 'Write')
        self.emit('write_enqueued', id=f'{frame.ident:03X}', data=frame.data.hex(' ').upper())

    def receive(self, seconds, phase, stop_on_data=False):
        end = time.perf_counter() + seconds
        counts, statuses = Counter(), Counter()
        echoes = 0
        while time.perf_counter() < end:
            msg, stamp = Msg(), Timestamp()
            status = self.api.CAN_Read(self.channel, ct.byref(msg), ct.byref(stamp))
            if status == 0x20:  # QRCVEMPTY
                time.sleep(.002)
                continue
            is_data = vehicle_data(status, msg.kind)
            statuses[f'0x{status:X}'] += 1
            # Echo, error and status records remain in the journal, not wake counts.
            self.emit('rx', phase=phase, status=f'0x{status:X}', kind=f'0x{msg.kind:X}',
                      id=f'{msg.ident:03X}', dlc=msg.length,
                      data=bytes(msg.data[:min(msg.length, 8)]).hex(' ').upper(),
                      adapter_us=stamp.micros + 1000 * (stamp.millis + (stamp.overflow << 32)))
            if msg.kind & 0x20:
                echoes += 1
            if is_data:
                counts[f'{msg.ident:03X}'] += 1
                if stop_on_data:
                    break
            if status:
                time.sleep(.002)
        result = {'data_frames': sum(counts.values()), 'ids': dict(counts),
                  'bsi_seen': bool(counts['432'] or counts['792']),
                  'echoes': echoes, 'read_statuses': dict(statuses),
                  'bus_status': f'0x{self.api.CAN_GetStatus(self.channel):X}'}
        self.emit('window', phase=phase, **result)
        return result

    def close(self):
        if self.opened:
            try:
                if self.hard_enabled:
                    self.reset()
            finally:
                status = self.api.CAN_Uninitialize(self.channel)
                self.opened = False
                self.emit('disconnected', status=f'0x{status:X}')
                self.checked(status, 'Uninitialize')


def vehicle_data(status, kind):
    # Reject RTR, local TX echo, error and status records as wake evidence.
    return status == 0 and not kind & (0x01 | 0x20 | 0x40 | 0x80)


def scan(io, frames, emit, tx_seconds=.2, observe_seconds=1., quiet_seconds=1.):
    """Every Write has a finally-protected hardware reset, including Ctrl+C."""
    results = []
    try:
        io.open()
        io.reset()
        initial = io.receive(max(2., quiet_seconds), 'initial_baseline', stop_on_data=True)
        if initial['data_frames']:
            emit('stop', reason='traffic_before_scan', observation=initial)
            return results
        if initial.get('bus_status', '0x0') != '0x0':
            raise RuntimeError('Controller is not clean after initial reset; aborting')
        for index, frame in enumerate(frames, 1):
            baseline = io.receive(quiet_seconds, 'baseline', stop_on_data=True)
            if baseline['data_frames']:
                emit('stop', reason='traffic_before_next_trial', next_id=f'{frame.ident:03X}',
                     previous_id=results[-1]['id'] if results else None)
                break
            if baseline.get('bus_status', '0x0') != '0x0':
                raise RuntimeError('Controller is not clean before next trial; aborting')
            emit('trial_start', index=index, total=len(frames), id=f'{frame.ident:03X}',
                 dlc=len(frame.data), data=frame.data.hex(' ').upper())
            try:
                io.write(frame)  # Exactly ONE API enqueue. Hardware retries may follow.
                during = io.receive(tx_seconds, 'tx', stop_on_data=True)
            finally:
                io.reset()  # Stop pending physical retries before observing/next ID.
            after = io.receive(observe_seconds, 'after_reset')
            candidate = bool(during['data_frames'] or after['data_frames'])
            result = {'id': f'{frame.ident:03X}', 'data': frame.data.hex(' ').upper(),
                      'candidate': candidate, 'during': during, 'after': after}
            results.append(result)
            emit('trial_result', **result)
            if candidate:
                emit('stop', reason='wake_candidate_repeat_to_confirm', id=f'{frame.ident:03X}')
                break
            if after.get('bus_status', '0x0') != '0x0':
                raise RuntimeError('Controller did not recover after hard reset; aborting')
        return results
    finally:
        io.close()  # Final reset + disconnect also on Write/read/reset exceptions.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true', help='Actually transmit; otherwise preview only')
    parser.add_argument('--profile', choices=['app', 'all'], default='app',
                        help='app: 68 MyOpel IDs; all: union with READY, 89 IDs')
    parser.add_argument('--only-id', type=lambda v: int(v, 16), action='append', help='Hex ID to retest')
    parser.add_argument('--start-id', type=lambda v: int(v, 16), default=0, help='Resume at hex ID')
    parser.add_argument('--channel', type=lambda v: int(v, 0), default=0x51)
    parser.add_argument('--dll', type=Path, help='Path to x64 PCANBasic.dll, API >=4.8')
    parser.add_argument('--tx-ms', type=int, default=200)
    parser.add_argument('--observe-ms', type=int, default=1000)
    parser.add_argument('--quiet-ms', type=int, default=1000)
    parser.add_argument('--output', type=Path, help='New JSONL journal path')
    args = parser.parse_args()
    for name in ['tx_ms', 'observe_ms', 'quiet_ms']:
        value = getattr(args, name)
        upper = 1000 if name == 'tx_ms' else 10000
        if not 10 <= value <= upper:
            parser.error(f'--{name.replace("_", "-")} must be 10..{upper}')
    frames = [f for f in frames_for(args.profile) if f.ident >= args.start_id]
    if args.only_id:
        available = {f.ident for f in frames}
        if set(args.only_id) - available:
            parser.error('Requested ID not in selected profile/range')
        frames = [f for f in frames if f.ident in args.only_id]
    if not frames:
        parser.error('No frames selected')
    if not args.execute:
        for frame in frames:
            print(f'{frame.ident:03X}  {len(frame.data)}  {frame.data.hex(" ").upper()}')
        print(f'PREVIEW ONLY: {len(frames)} IDs, no PCAN access. Add --execute to run.')
        return 0
    root = Path(__file__).resolve().parents[1]
    dll = args.dll or os.environ.get('PCAN_BASIC_DLL')
    if not dll:
        cached = root / 'build-host-tests' / 'pcan-sdk' / 'PCANBasic.dll'
        dll = cached if cached.exists() else ctypes.util.find_library('PCANBasic')
    if not dll:
        parser.error('Provide --dll or PCAN_BASIC_DLL pointing to x64 PCANBasic.dll')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = args.output or root / 'logs' / 'wake-scan' / f'{stamp}-{args.profile}.jsonl'
    journal = Journal(output)
    try:
        journal('configuration', profile=args.profile, frames=len(frames),
                tx_ms=args.tx_ms, observe_ms=args.observe_ms, quiet_ms=args.quiet_ms,
                channel=f'0x{args.channel:X}', dll=str(dll),
                one_api_write_per_id=True, controller_retry_count='not measured')
        io = Pcan(dll, args.channel, journal)
        results = scan(io, frames, journal, args.tx_ms / 1000,
                       args.observe_ms / 1000, args.quiet_ms / 1000)
        journal('complete', tested=len(results), selected=len(frames),
                candidate=next((r['id'] for r in results if r['candidate']), None))
        return 0
    except KeyboardInterrupt:
        journal('interrupted')
        return 130
    except Exception as error:
        journal('failed', message=str(error))
        return 1
    finally:
        journal.close()
        print(f'Journal: {output}', flush=True)


if __name__ == '__main__':
    raise SystemExit(main())
