"""Checks a Fluante on its USB serial port, through TesserAPI's serial
transport (envelopes, one per line), without turning the motor.

Usage:
  uv run --with pyserial python test/e2e_board.py PORT [--reset] [--mac MAC]

With --reset, restarts the board first and waits for it to answer. With
--mac, refuses a board whose MAC differs: boards with the same USB serial
number swap port names when they re-enumerate. Reads the
device's status, the TMC2209's (whether it answers on its UART, its version
and flags), the strobe's applied timing; writes the strobe's frequency and
duty and reads them back, then restores them. The spinner must be stopped:
the script refuses to go on otherwise, and never writes /spinner/running.
Prints "E2E pass=<n> fail=<n>".
"""
import json
import sys
import time

import serial


class Board:
    def __init__(self, port, reset):
        # DTR/RTS deasserted: asserting them resets boards with an auto-reset
        # circuit.
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 0.1
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        self.buf = b''
        self.next_id = 1
        if reset:
            self.s.rts = True
            time.sleep(0.1)
            self.s.rts = False
            time.sleep(4)
        self.s.reset_input_buffer()
        self.s.write(b'\n')
        time.sleep(0.2)

    def lines(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            self.buf += self.s.read(max(1, self.s.in_waiting))
            while b'\n' in self.buf:
                line, self.buf = self.buf.split(b'\n', 1)
                yield line.decode('utf-8', 'replace').rstrip('\r')

    def call(self, op, path, body=None, timeout=5):
        i = self.next_id
        self.next_id += 1
        env = {'id': i, 'op': op, 'path': path}
        if body is not None:
            env['body'] = body
        self.s.write(json.dumps(env).encode() + b'\n')
        for line in self.lines(timeout):
            if not line.startswith('{'):
                continue  # the log
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get('id') == i:
                return msg.get('status'), msg.get('body')
        return 'timeout', None

    def get(self, path):
        return self.call('get', path)

    def set(self, path, body):
        return self.call('set', path, body)


passed = failed = 0


def check(name, ok, detail=''):
    global passed, failed
    if ok:
        passed += 1
        print(f'PASS {name}')
    else:
        failed += 1
        print(f'FAIL {name} {detail}')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    b = Board(sys.argv[1], '--reset' in sys.argv)

    st, status = b.get('/sys/status')
    check('device answers', st == 'ok', st)
    if st != 'ok':
        print(f'E2E pass={passed} fail={failed}')
        return 1
    print('  status:', json.dumps(status))
    if '--mac' in sys.argv and status.get('mac') != sys.argv[sys.argv.index('--mac') + 1].lower():
        print(f'not the board asked for: {status.get("mac")}')
        return 1

    st, spinner = b.get('/spinner')
    check('spinner tree', st == 'ok', st)
    if spinner.get('running') or spinner.get('state') not in ('stopped', 'halted'):
        print('the spinner is running: stop it first (this script never turns the motor)')
        return 1
    print('  spinner:', json.dumps(spinner))

    st, drv = b.get('/driver/status')
    check('driver status', st == 'ok', st)
    print('  driver:', json.dumps(drv))
    check('TMC2209 answers on its UART', bool(drv and drv.get('present')), json.dumps(drv))
    if drv and drv.get('present'):
        st, regs = b.get('/driver/registers')
        if st == 'ok':
            by_name = {r['name']: r for r in regs} if isinstance(regs, list) else regs
            ioin = by_name.get('IOIN', {})
            print('  IOIN:', ioin.get('fields'))
            check('TMC2209 version 0x21', 'version=33' in str(ioin.get('fields')), str(ioin))
            for name in ('GCONF', 'CHOPCONF', 'PWMCONF', 'DRV_STATUS'):
                r = by_name.get(name, {})
                print(f'  {name}: 0x{r.get("value", 0):08x} {r.get("fields")}')
        check('driver disabled', not drv.get('driverError') and not drv.get('overtemperature'), json.dumps(drv))

    st, strobe = b.get('/strobe')
    check('strobe tree', st == 'ok', st)
    print('  strobe:', json.dumps(strobe))
    freq0, duty0, on0 = strobe['frequency'], strobe['duty'], strobe['on']
    st, _ = b.set('/strobe', {'on': True, 'frequency': 48.5, 'duty': 2})
    check('strobe set', st == 'ok', st)
    st, applied = b.get('/strobe/applied')
    check('strobe applied 48.5 Hz', st == 'ok' and abs(applied['frequency'] - 48.5) < 0.001, json.dumps(applied))
    check('strobe pulse 412 µs', st == 'ok' and abs(applied['pulse'] - 412) < 5, json.dumps(applied))
    st, _ = b.set('/strobe', {'on': on0, 'frequency': freq0, 'duty': duty0})
    check('strobe restored', st == 'ok', st)

    st, peers = b.get('/peers')
    print('  peers:', json.dumps(peers) if st == 'ok' else st)

    print(f'E2E pass={passed} fail={failed}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
