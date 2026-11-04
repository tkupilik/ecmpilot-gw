"""Seed/release varstore test inputs and inspect the gateway over real UDP. Standard library only."""
import argparse
import json
import socket
import urllib.request
import uuid

SEED = {
    'vehicle.awake': 1, 'vehicle.parked': 1, 'battery.soc_pct_x10': 682,
    'vehicle.range_km_x10': 2180, 'temperature.cabin_mc': 16000,
    'temperature.outside_mc': 9000, 'battery.min_mc': 24000, 'battery.max_mc': 27000,
    'vehicle.odometer_km': 42810, 'battery.voltage_mv': 360000, 'battery.current_ma': 0,
    'climate.state': 0, 'charge.state': 1, 'charge.mode': 0, 'charge.connected': 1,
    'charge.power_w': 0, 'charge.remaining_min': 135, 'charge.start_minute': 1380,
    'test.enabled': 1,
}


def http(host, method='GET', payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    request = urllib.request.Request(f'http://{host}/api/vars', data=data, method=method,
                                     headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)


def exchange(sock, op, gateway=None):
    request = dict(v=0, type='request', gw=gateway, client=uuid.uuid4().hex,
                   id=uuid.uuid4().hex, op=op, args={})
    sock.send(json.dumps(request,separators=(',',':')).encode())
    data = sock.recv(1025)
    if len(data)>1024:
        raise ValueError('Oversized response')
    response = json.loads(data)
    if response.get('reply_to') != request['id'] or response.get('type') != 'status' or response.get('v') != 0:
        raise ValueError('Unexpected UDP response')
    if gateway is not None and response.get('gw') != gateway:
        raise ValueError('Gateway identity changed')
    if response.get('error'):
        raise ValueError(response['error'])
    return response


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',required=True,help='Gateway LAN address (without http://)')
    parser.add_argument('--port',type=int,default=42840)
    parser.add_argument('action',choices=['seed','release','info','state','set'])
    parser.add_argument('signal',nargs='?')
    parser.add_argument('value',nargs='?',type=int)
    args=parser.parse_args()
    if args.action in ('seed','release','set'):
        signals={s['name']:s['id'] for s in http(args.host)['signals']}
        values=SEED if args.action!='set' else {args.signal:args.value}
        if args.action=='set' and (args.signal is None or args.value is None):
            parser.error('set requires SIGNAL VALUE')
        for name,value in values.items():
            if name not in signals:
                raise ValueError(f'Unknown signal: {name}')
            if not -2147483648 <= value <= 2147483647:
                raise ValueError('Value outside int32 range')
            http(args.host,'POST',{'id':signals[name],'action':'release' if args.action=='release' else 'force','value':value})
        print('Test overrides released.' if args.action=='release' else 'Varstore test inputs applied; no vehicle commands sent.')
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
        sock.settimeout(2);sock.connect((args.host,args.port))
        info=exchange(sock,'info.get')
        response=exchange(sock,'state.get',info['gw']) if args.action=='state' else info
        print(json.dumps(response,indent=2))


if __name__=='__main__':
    main()
