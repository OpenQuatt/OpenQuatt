#!/usr/bin/env python3
"""Bounded lab-only CIC HTTP and MQTT 3.1.1 QoS0 input fixture, no credentials."""
import argparse
import json
import ipaddress
import socketserver
import struct
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

lock = threading.Lock()
feed = {'thermostat': {'otFtRoomTemperature': 19, 'otFtRoomSetpoint': 18, 'otFtChEnabled': False, 'otFtCoolingEnabled': False}, 'flowMeter': {'waterSupplyTemperature': 25}}
http_status = 200
clients = set()


def emit(value):
    print(json.dumps(value), flush=True)


class CicHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.client_address[0] != '192.168.2.86' or self.path != '/beta/feed/data.json':
            self.send_error(404)
            return
        with lock:
            body = json.dumps(feed, separators=(',', ':')).encode()
            status = http_status
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def encode_length(length):
    result = bytearray()
    while True:
        value = length % 128
        length //= 128
        result.append(value | (128 if length else 0))
        if not length:
            return bytes(result)


def publish_packet(topic, value):
    encoded = topic.encode()
    body = struct.pack('!H', len(encoded)) + encoded + str(value).encode()
    return b'\x30' + encode_length(len(body)) + body


class MqttHandler(socketserver.BaseRequestHandler):
    def handle(self):
        if self.client_address[0] != '192.168.2.86':
            return
        self.request.settimeout(30)
        self.topics = set()
        self.send_lock = threading.Lock()
        with lock:
            clients.add(self)
        try:
            while True:
                first = self.read_exact(1)[0]
                remaining, multiplier = 0, 1
                for _ in range(4):
                    value = self.read_exact(1)[0]
                    remaining += (value & 127) * multiplier
                    if not value & 128:
                        break
                    multiplier *= 128
                else:
                    raise ValueError('invalid MQTT length')
                if remaining > 8192:
                    raise ValueError('MQTT packet exceeds fixture bound')
                body = self.read_exact(remaining)
                kind = first >> 4
                if kind == 1:
                    # Reject credential-bearing CONNECT packets; fixture never logs payloads.
                    if len(body) < 10 or body[7] & 0xC0:
                        self.send(b'\x20\x02\x00\x05')
                        return
                    self.send(b'\x20\x02\x00\x00')
                elif kind == 8:
                    packet_id, offset, granted = body[:2], 2, bytearray()
                    while offset < len(body):
                        length = struct.unpack('!H', body[offset:offset+2])[0]
                        self.topics.add(body[offset+2:offset+2+length].decode())
                        offset += 2 + length
                        if offset >= len(body):
                            raise ValueError('invalid SUBSCRIBE')
                        granted.append(0)
                        offset += 1
                    response = packet_id + granted
                    self.send(b'\x90' + encode_length(len(response)) + response)
                elif kind == 10:
                    self.send(b'\xB0\x02' + body[:2])
                elif kind == 12:
                    self.send(b'\xD0\x00')
                elif kind == 14:
                    return
                elif kind == 3 and ((first >> 1) & 3) == 1:
                    topic_len = struct.unpack('!H', body[:2])[0]
                    self.send(b'\x40\x02' + body[2+topic_len:4+topic_len])
        except (OSError, ValueError, IndexError, struct.error):
            pass
        finally:
            with lock:
                clients.discard(self)

    def send(self, packet):
        with self.send_lock:
            self.request.sendall(packet)

    def read_exact(self, count):
        result = bytearray()
        while len(result) < count:
            data = self.request.recv(count - len(result))
            if not data:
                raise OSError('connection closed')
            result.extend(data)
        return bytes(result)


class MqttServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    global feed, http_status
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', required=True)
    args = parser.parse_args()
    if ipaddress.IPv4Address(args.host) not in ipaddress.IPv4Network('192.168.2.0/24') or args.host in ['192.168.2.0', '192.168.2.63', '192.168.2.86', '192.168.2.255']:
        raise SystemExit('fixture must bind a desktop address in the documented lab subnet')
    http = ThreadingHTTPServer((args.host, 0), CicHandler)
    mqtt = MqttServer((args.host, 0), MqttHandler)
    for server in [http, mqtt]:
        threading.Thread(target=server.serve_forever, daemon=True).start()
    emit({'type': 'ready', 'http_url': f'http://{args.host}:{http.server_port}/beta/feed/data.json', 'mqtt_port': mqtt.server_address[1]})
    try:
        for line in sys.stdin:
            if len(line) > 4096:
                raise ValueError('fixture command exceeds bound')
            message = json.loads(line)
            if message.get('stop'):
                break
            with lock:
                if 'feed' in message:
                    encoded = json.dumps(message['feed']).encode()
                    if len(encoded) > 2048:
                        raise ValueError('CIC body exceeds parser bound')
                    feed = message['feed']
                if 'http_status' in message:
                    http_status = int(message['http_status'])
                for topic, value in message.get('publish', {}).items():
                    packet = publish_packet(topic, value)
                    for client in list(clients):
                        if topic not in client.topics:
                            continue
                        try:
                            client.send(packet)
                        except OSError:
                            clients.discard(client)
            emit({'type': 'ack', 'id': message.get('id')})
    finally:
        http.shutdown()
        mqtt.shutdown()
        http.server_close()
        mqtt.server_close()


if __name__ == '__main__':
    main()
