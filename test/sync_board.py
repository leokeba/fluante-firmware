"""Watches a Fluante's lock to network time on its USB serial port
(docs/DESIGN.md section 5.4), without turning the motor.

Usage:
  uv run --with pyserial python test/sync_board.py PORT [--seconds S]
      [--mac MAC] [--reset]

Turns /strobe/sync/on on if it was off, then samples /strobe/sync and NowTP's
time status every second: the lock's state, its error and its trim. Passes
when it locked and held within 100 µs (or a count of the timer) for the last
30 s; a trim within ±40 ppm says the counter's sample and the trim agree with
the crystal (the LEDC sample's slope and the trim's direction). Prints
"SYNC pass=<n> fail=<n>". The phase between two Fluantes needs a scope
(section 10).
"""
import json
import sys
import time

from e2e_board import Board


def arg(name, default):
    if name in sys.argv:
        return type(default)(sys.argv[sys.argv.index(name) + 1])
    return default


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    seconds = arg('--seconds', 120)
    b = Board(sys.argv[1], '--reset' in sys.argv)
    st, status = b.get('/sys/status')
    if st != 'ok':
        print('the device does not answer:', st)
        return 1
    if '--mac' in sys.argv and status.get('mac') != sys.argv[sys.argv.index('--mac') + 1].lower():
        print(f'not the board asked for: {status.get("mac")}')
        return 1
    st, strobe = b.get('/strobe')
    if st != 'ok' or 'sync' not in strobe:
        print('no /strobe/sync: an older firmware?', st)
        return 1
    if not strobe['sync']['on']:
        b.set('/strobe/sync/on', True)
    period = 1e6 / strobe['applied']['frequency']
    step = period / (1 << strobe['applied']['bits'])
    print(f'  strobe: {json.dumps(strobe)}')

    rows = []
    start = time.time()
    while time.time() - start < seconds:
        t = time.time() - start
        st, sync = b.get('/strobe/sync')
        _, nowtp = b.get('/sys/nowtp/status')
        if st == 'ok':
            rows.append((t, sync))
            print(f'  {t:6.1f} s  {sync["state"]:9}  error {sync["error"]:9.1f} µs  trim {sync["trim"]:7.2f} ppm'
                  f'  nowtp {json.dumps(nowtp)[:120]}')
        time.sleep(max(0.0, 1 - (time.time() - start - t)))

    passed = failed = 0

    def check(name, ok, detail=''):
        nonlocal passed, failed
        passed += ok
        failed += not ok
        print(f'{"PASS" if ok else "FAIL"} {name} {detail}')

    tail = [s for t, s in rows if t >= seconds - 30]
    limit = max(100, step)
    check('locked', any(s['state'] == 'locked' for s in tail), f'({tail[-1]["state"] if tail else "no samples"})')
    worst = max((abs(s['error']) for s in tail), default=float('inf'))
    check('held', worst <= limit, f'(worst {worst:.1f} µs over the last 30 s, limit {limit:.0f})')
    trims = [s['trim'] for s in tail]
    check('trim within the crystals', bool(trims) and all(abs(x) <= 40 for x in trims),
          f'({min(trims, default=0):.1f} to {max(trims, default=0):.1f} ppm)')
    print(f'SYNC pass={passed} fail={failed}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
