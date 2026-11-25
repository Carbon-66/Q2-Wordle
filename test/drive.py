#!/usr/bin/env python3
"""Runs the host build of q2-wordle as Q2 Pod would (fake /dev/fb0 from fbshim.so, input as text
datagrams on /tmp/q2-wordle.input.sock, TERM to end it) through one scenario, and prints what it
logged and its state. Frames are saved as OUT_DIR/<scenario>-<name>.png.

The Daily's word comes from a folder of JSON files, as the New York Times serves them, through
libcurl's file:// (WORDLE_URL_BASE), so no network is needed.

Usage: drive.py BUILD_DIR OUT_DIR SCENARIO
Scenarios: home, random, lose, hard, daily, offline, cached, screens, touch"""
import datetime, json, os, shutil, signal, socket, subprocess, sys, tempfile, time

build, outdir, scenario = sys.argv[1:4]
build, outdir = os.path.abspath(build), os.path.abspath(outdir)
sys.path.insert(0, os.path.dirname(__file__))
SOCK = '/tmp/q2-wordle.input.sock'
card = tempfile.mkdtemp()
app = os.path.join(card, '.wordle')
os.mkdir(app)
shutil.copy(os.path.join(build, 'q2-wordle'), app)
srv = os.path.join(card, 'nyt')
os.mkdir(srv)
today = datetime.date.today()

def serve(day, word, number):
    json.dump({'id': 9000 + number, 'solution': word, 'print_date': day.isoformat(),
               'days_since_launch': number, 'editor': 'Test'}, open(os.path.join(srv, day.isoformat() + '.json'), 'w'))

state_file = os.path.join(card, 'state.txt')
env = dict(os.environ, LD_PRELOAD=os.path.join(build, 'fbshim.so'), FB_DUMP=os.path.join(card, 'fb.raw'),
           FB_FINAL=os.path.join(card, 'fb.final'), WD_STATE=state_file,
           WORDLE_URL_BASE='file://' + srv + '/', WD_RANDOM_WORD='crane')
if os.environ.get('DRIVE_HTTP'):  # the same files over HTTP, for the headers' path
    import http.server, threading, functools
    skew = int(os.environ.get('DRIVE_SKEW_DAYS', '0'))  # the server's clock this many days ahead of ours
    class H(http.server.SimpleHTTPRequestHandler):
        def date_time_string(self, timestamp=None):
            return super().date_time_string(time.time() + skew * 86400)
        def log_message(self, *a):
            pass
    httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(H, directory=srv))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    env['WORDLE_URL_BASE'] = f'http://127.0.0.1:{httpd.server_address[1]}/'
results = {}
proc = None

def start():
    global proc
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    if os.path.exists(state_file):
        os.unlink(state_file)
    proc = subprocess.Popen([os.path.join(app, 'q2-wordle')], env=env, cwd=app)
    wait_for(lambda: os.path.exists(SOCK), 5, 'input socket')
    wait_for(lambda: os.path.exists(state_file), 10, 'first frame')

def stop():
    proc.send_signal(signal.SIGTERM)
    status = proc.wait(5)
    print('exit status:', status)
    print('panel:', open(os.path.join(card, 'fb.final')).read().strip())
    print('socket left:', os.path.exists(SOCK))

def wait_for(cond, secs, what):
    end = time.time() + secs
    while time.time() < end:
        if cond():
            return
        time.sleep(0.03)
    if proc:
        proc.kill()
    log = os.path.join(app, 'wordle.log')
    sys.exit(f'FAIL: no {what} after {secs} s\n' + (open(log).read() if os.path.exists(log) else ''))

def state():
    out = {}
    try:
        for line in open(state_file):
            k, _, v = line.rstrip('\n').partition(' ')
            out[k] = v
    except OSError:
        pass
    return out

def st(k):
    return state().get(k)

s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
def send(m, pause=0.06):
    s.sendto(m.encode(), SOCK)
    time.sleep(pause)

def key(code, pause=0.08):
    send(f'k d {code}', 0.02)
    send(f'k u {code}', pause)

CENTRE, RETURN = 218, 170
def centre(p=0.1): key(CENTRE, p)
def back(p=0.1): key(RETURN, p)
def wheel(steps):  # clockwise steps of the highlight: 20 units each, falling clockwise
    for _ in range(abs(steps)):
        send(f'w {-20 if steps > 0 else 20}', 0.015)
    time.sleep(0.08)

def tap(x, y, p=0.1):
    send(f't d {x} {y}', 0.03)
    send(f't u {x} {y}', p)

def shot(name):
    time.sleep(0.12)
    from view import convert
    convert(os.path.join(card, 'fb.raw'), os.path.join(outdir, f'{scenario}-{name}.png'))

def result(k, v):
    results[k] = v
    print(f'result: {k}: {v}')

# the keyboard's centres, as game.c lays them out
def key_centre(ch):
    rows = ['qwertyuiop', 'asdfghjkl', 'zxcvbnm']
    if ch in rows[0]:
        i = rows[0].index(ch); x0 = (375 - (10 * 33 + 9 * 4)) // 2
        return x0 + i * 37 + 16, 205 + 14
    if ch in rows[1]:
        i = rows[1].index(ch); x0 = (375 - (9 * 33 + 8 * 4)) // 2
        return x0 + i * 37 + 16, 205 + 32 + 14
    x0 = (375 - (2 * 42 + 7 * 31 + 8 * 3)) // 2
    y = 205 + 64 + 14
    if ch == 'enter': return x0 + 21, y
    if ch == 'back': return x0 + 42 + 3 + 7 * 34 + 21, y
    i = rows[2].index(ch)
    return x0 + 45 + i * 34 + 15, y

def type_touch(word, enter=True):
    for ch in word:
        tap(*key_centre(ch), 0.05)
    if enter:
        tap(*key_centre('enter'), 0.1)

# the highlight's order: q..p a..l ENTER z..m BACK
ORDER = list('qwertyuiopasdfghjkl') + ['enter'] + list('zxcvbnm') + ['back']
def type_wheel(word, enter=True):
    for ch in list(word) + (['enter'] if enter else []):
        cur = int(st('cursor'))
        want = ORDER.index(ch)
        d = (want - cur) % 28
        if d > 14: d -= 28
        wheel(d)
        assert int(st('cursor')) == want, (ch, st('cursor'))
        centre(0.06)

def settle(secs=2.2):
    time.sleep(secs)

start()
if scenario == 'home':
    shot('home')
    wheel(1); result('home_sel', st('home_sel'))
    wheel(-2); result('home_sel_wrap', st('home_sel'))
elif scenario == 'random':
    wheel(1); centre(0.3)
    result('screen', st('screen')); result('mode', st('mode'))
    shot('empty')
    type_wheel('slate', enter=False)
    shot('typed')
    result('cursor_after_five', st('cursor'))
    centre(0.1)
    time.sleep(0.45); shot('reveal')
    settle(); shot('row1')
    result('rows_after_one', st('rows'))
    type_touch('xyzzy')
    time.sleep(0.15); result('toast_bad', st('toast')); shot('notword')
    for _ in range(5): back(0.04)
    type_touch('cra', enter=True)
    time.sleep(0.15); result('toast_short', st('toast'))
    for _ in range(3): back(0.04)
    type_touch('trace'); settle()
    type_touch('crane'); time.sleep(2.0); shot('won')
    time.sleep(1.5)
    result('done', st('done')); result('rows', st('rows')); result('stats_over', st('stats_over'))
    result('played', st('played')); result('won', st('won'))
    shot('stats')
    centre(0.4)  # new word
    result('new_rows', st('rows')); result('new_done', st('done'))
    back(0.2); result('screen_after_back', st('screen'))
elif scenario == 'lose':
    wheel(1); centre(0.3)
    for w in ['about', 'other', 'which', 'their', 'there', 'would']:
        type_touch(w); settle(2.0)
    time.sleep(1.8)
    result('done', st('done')); result('toast', st('toast')); shot('lost')
    time.sleep(1.0); shot('stats')
    result('won', st('won')); result('played', st('played'))
elif scenario == 'hard':
    wheel(4); centre(0.3)  # Settings
    result('screen', st('screen'))
    centre(0.2); result('hard', st('hard'))
    shot('settings')
    back(0.2); wheel(-3); centre(0.3)  # Random
    result('mode', st('mode'))
    type_touch('cabin'); settle()  # c green, a: crane's a is 3rd: yellow; n yellow
    type_touch('motel'); time.sleep(0.2)
    result('toast_hard', st('toast')); result('rows', st('rows'))
    shot('hardmsg')
elif scenario == 'daily':
    serve(today, 'plumb', 1621)
    for i in range(1, 4):
        serve(today + datetime.timedelta(days=i), 'quart', 1621 + i)
    centre(0.1)
    wait_for(lambda: st('screen') == '2', 10, 'the daily board')
    result('number', st('number')); result('answer', st('answer')); result('date', st('date'))
    type_touch('plumb'); time.sleep(4.0)
    result('done', st('done')); shot('stats')
    time.sleep(0.5)
    cache = open(os.path.join(app, 'wordle.cache')).read().split('\n')
    result('cache_lines', len([l for l in cache if l.strip()]))
    back(0.2); back(0.2)
    result('screen_home', st('screen'))
    shot('home_after')
    centre(0.3)  # back into today's: no fetch needed
    result('screen_again', st('screen')); result('stats_again', st('stats_over'))
elif scenario == 'skew':
    serve(today, 'plumb', 1621)
    serve(today + datetime.timedelta(days=2), 'quart', 1623)
    centre(0.1)
    wait_for(lambda: st('screen') == '2', 10, 'the daily board')
    result('answer', st('answer')); result('number', st('number')); result('date', st('date'))
elif scenario == 'offline':
    centre(0.1)
    wait_for(lambda: st('fetch_failed') == '1', 10, 'the failure')
    shot('failed')
    wheel(1); centre(0.3)  # Play a Random Word
    result('mode', st('mode')); result('screen', st('screen'))
elif scenario == 'cached':
    # a cache written by an earlier day's fetch, today's word in it; no JSON served
    def hide(w):
        return ''.join(chr(97 + (ord(c) - 97 + 7 + 3 * i) % 26) for i, c in enumerate(w))
    open(os.path.join(app, 'wordle.cache'), 'w').write(f'{today.isoformat()} 1700 {hide("ghost")}\n')
    centre(0.1)
    wait_for(lambda: st('screen') == '2', 10, 'the cached board')
    result('answer', st('answer')); result('number', st('number'))
    shot('cached')
elif scenario == 'screens':
    wheel(2); centre(0.2); shot('help')
    send('w -10', 0.02); seen = int(st('help_y'))
    time.sleep(0.4); result('help_small', st('help_y'))  # half a step's travel still scrolls: 12 px
    result('help_eased', 1 if seen < 12 else 0)
    for _ in range(6): wheel(1)
    shot('help_scrolled')
    wheel(60)
    shot('help_end')
    wheel(-80)
    back(0.2); wheel(1); centre(0.2); shot('stats')
    back(0.2); wheel(1); centre(0.2)
    wheel(1); centre(0.2); result('dark', st('dark')); shot('settings_dark')
    back(0.2); wheel(-3); centre(0.3)
    type_touch('slate'); settle(); type_touch('crony'); settle()
    shot('dark_board')
    back(0.1); back(0.1)
    result('screen', st('screen'))
elif scenario == 'touch':
    wheel(1); centre(0.3)
    send('t d %d %d' % key_centre('q'), 0.05)
    send('t m %d %d' % key_centre('w'), 0.1)
    shot('slide')
    send('t u %d %d' % key_centre('w'), 0.1)
    result('ncur', st('ncur'))
    tap(187, 100, 0.1)  # the board: nothing
    result('ncur_board', st('ncur'))
    tap(60, 12, 0.2)  # the header's back
    result('screen', st('screen'))
stop()
log = os.path.join(app, 'wordle.log')
print('--- log ---')
print(open(log).read())
