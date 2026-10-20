"""Turns a Fluante for a while on its USB serial port, and prints what the
TMC2209 reports as it starts, turns and stops. A hand test: it turns the
motor, so run it only with Leo's go-ahead.

Usage:
  uv run --with pyserial python test/turn_board.py PORT [--speed RPM]
      [--current A] [--accel RPM/S] [--seconds S] [--mac MAC]

Sets the run current, the speed and the acceleration, turns /spinner/running
on, samples every half second (the spinner's state and speed; DRV_STATUS's
standstill, stealthChop and current scale; TSTEP; PWM_SCALE and PWM_AUTO,
stealthChop's automatic tuning; SG_RESULT), then turns it off and samples
the ramp down. Restores the settings it changed, except running (off).
"""
import json
import sys
import time

from e2e_board import Board


def arg(name, default):
    if name in sys.argv:
        return type(default)(sys.argv[sys.argv.index(name) + 1])
    return default


def registers(b):
    st, regs = b.get('/driver/registers')
    if st != 'ok':
        return {}
    return {r['name']: r for r in regs}


def sample(b, t0):
    _, sp = b.get('/spinner')
    _, drv = b.get('/driver/status')
    _, actual = b.get('/driver/actualVelocity')
    regs = registers(b)
    f = lambda name: regs.get(name, {}).get('fields', '')
    v = lambda name: regs.get(name, {}).get('value', 0)
    print(f'{time.time() - t0:5.1f}s {sp["state"]:9} rpm={sp["rpm"]:7.2f} '
          f'stst={int(drv["standstill"])} stealth={int(drv["stealthChop"])} cs={drv["current"]:2} '
          f'err={drv["errors"]} v={actual} TSTEP={v("TSTEP")} | {f("PWM_SCALE")} | {f("PWM_AUTO")} | SG={v("SG_RESULT")}')
    return sp, drv


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    speed = arg('--speed', 30.0)
    current = arg('--current', 0.5)
    accel = arg('--accel', 30.0)
    seconds = arg('--seconds', 8.0)
    b = Board(sys.argv[1], False)
    _, status = b.get('/sys/status')
    if '--mac' in sys.argv and (status or {}).get('mac') != arg('--mac', '').lower():
        print(f'not the board asked for: {(status or {}).get("mac")}')
        return 1

    _, sp0 = b.get('/spinner')
    _, cfg0 = b.get('/driver/config')
    if sp0.get('running'):
        print('already running: stop it first')
        return 1
    _, drv = b.get('/driver/status')
    if not drv or not drv.get('present'):
        print('the driver doesn\'t answer')
        return 1

    print(b.set('/driver/config/runCurrent', current))
    print(b.set('/spinner', {'speed': speed, 'acceleration': accel}))
    t0 = time.time()
    try:
        print(b.set('/spinner/running', True))
        end = t0 + seconds
        while time.time() < end:
            sample(b, t0)
            time.sleep(0.5)
    finally:
        print(b.set('/spinner/running', False))
    end = time.time() + abs(speed) / accel + 1.5 if accel > 0 else time.time() + 1.5
    while time.time() < end:
        sp, _ = sample(b, t0)
        time.sleep(0.5)
    print(b.set('/spinner', {'speed': sp0['speed'], 'acceleration': sp0['acceleration']}))
    print(b.set('/driver/config/runCurrent', cfg0['runCurrent']))
    _, sp = b.get('/spinner')
    print('final:', json.dumps(sp))
    return 0


if __name__ == '__main__':
    sys.exit(main())
