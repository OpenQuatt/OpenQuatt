"""Local HIL HA ingress through the actual encrypted ESPHome native API.

The key is passed in the environment, never printed or recorded in the report.
This is a protocol fixture, not a live Home Assistant installation.
"""
import argparse
import asyncio
import json
import os
import sys
import time

from aioesphomeapi import APIClient


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', required=True)
    args = parser.parse_args()
    if args.host not in ('192.168.2.86', 'openquatt-test.local'):
        raise SystemExit('HA fixture only supports the documented HCQ desktop testcontroller')
    key = os.environ.get('OQ_HIL_NATIVE_API_KEY')
    if not key:
        raise SystemExit('OQ_HIL_NATIVE_API_KEY must be supplied privately for the HIL-only firmware')
    values = {'room': 19.0, 'setpoint': 18.0, 'supply': 25.0, 'outside': 8.0, 'heat': False, 'cool': False, 'dew': 10.0}
    stop = asyncio.Event()
    client = None

    def emit(kind, **payload):
        print(json.dumps({'type': kind, **payload}), flush=True)

    def publish():
        if client is None:
            return
        states = {
            'sensor.openquatt_ext_room_temperature': str(values['room']),
            'sensor.openquatt_ext_room_setpoint': str(values['setpoint']),
            'sensor.openquatt_ext_water_supply_temperature': str(values['supply']),
            'sensor.openquatt_ext_outdoor_temperature': str(values['outside']),
            'sensor.openquatt_ext_cooling_dew_point': str(values['dew']),
            'sensor.openquatt_ext_heat_demand': '0',
            'sensor.openquatt_ext_heating_supply_target': '40',
            'sensor.openquatt_ext_heating_curve_modifier': '0',
            'sensor.openquatt_ha_ingress_heartbeat': str(int(time.time())),
        }
        for name in ('outdoor_temperature', 'water_supply_temperature', 'room_temperature', 'room_setpoint', 'heating_enable', 'cooling_enable', 'cooling_dew_point', 'heat_demand', 'heating_supply_target', 'heating_curve_modifier'):
            states['binary_sensor.openquatt_ext_' + name + '_valid'] = 'on'
        states['binary_sensor.openquatt_ext_heating_enable'] = 'on' if values['heat'] else 'off'
        states['binary_sensor.openquatt_ext_cooling_enable'] = 'on' if values['cool'] else 'off'
        for entity_id, value in states.items():
            client.send_home_assistant_state(entity_id, None, value)

    async def commands():
        while not stop.is_set():
            line = await asyncio.to_thread(sys.stdin.readline)
            if not line:
                stop.set()
                return
            message = json.loads(line)
            if message.get('stop'):
                stop.set()
                return
            for field, value in message.get('values', {}).items():
                if field not in values:
                    raise ValueError('unsupported HA fixture field')
                values[field] = value
            if client is not None:
                publish()
            emit('ack', id=message.get('id'), values=values)

    reader = asyncio.create_task(commands())
    try:
        while not stop.is_set():
            if client is None or not client.is_connected:
                candidate = APIClient(args.host, 6053, None, noise_psk=key, expected_name='openquatt-test', client_info='OpenQuatt HIL native HA fixture')
                try:
                    await candidate.connect(login=True)
                    info = await candidate.device_info()
                    if info.name != 'openquatt-test' or info.project_name != 'openquatt.test':
                        raise RuntimeError('unexpected controller identity')
                    client = candidate
                    client.subscribe_home_assistant_states(lambda entity_id, attribute: publish())
                    client.subscribe_states(lambda state: None)
                    emit('ready', name=info.name, project=info.project_name, mac=info.mac_address)
                    publish()
                except Exception as error:
                    # Report only exception type; never include secrets from a
                    # connection exception or full client configuration.
                    emit('retry', error=type(error).__name__)
                    await candidate.disconnect()
                    client = None
            if client is not None:
                publish()
            try:
                await asyncio.wait_for(stop.wait(), timeout=5)
            except asyncio.TimeoutError:
                pass
    finally:
        if client is not None:
            await client.disconnect()
        reader.cancel()


if __name__ == '__main__':
    asyncio.run(main())
