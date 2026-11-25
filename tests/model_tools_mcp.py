"""Plan Paint's model tools over MCP (GUI.Forms round 4 of the pilot).

Starts Plan Paint with --mcp in a scratch home, then over MCP's stdio
framing: a new 200 by 120 picture; a filled, rotated rectangle; a filled
Bezier path; a bucket fill; a gradient; text; plan_paint_sample and capture
points reading the colours back, in both point forms ({x, y} and [x, y],
which the schemas also take); a radial gradient's radius; text's drawn size
and the measure option; the Resize dialog, its OK marked default, a press on
OK refused and the dialog closed by the model (a model never submits a
dialog); a 100-step batch of strokes. Each drawing is checked by
sampling. Every plan_paint_* and command_* tool, and each of its
arguments, is described (`help <tool>` is that text, so an undocumented tool
fails here). Ends with a hard disconnect that discards the picture and
expects exit 0 within the timeout. Then a second launch over a home holding an unfinished-artwork snapshot opens no
dialog: it starts with a new picture and says in the status bar that the
artwork can be recovered from the File menu; with nothing unsaved, the end
of its input closes it (exit 0). Exit 77 skips where no window
server is reachable.

    python3 -B tests/model_tools_mcp.py <plan-paint executable> [timeout seconds]
"""

import json
import os
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time

PROTOCOL_VERSION = '2025-06-18'


def reader(stream, replies):
    for raw in iter(stream.readline, b''):
        try:
            message = json.loads(raw.decode('utf-8'))
        except ValueError:
            continue
        if isinstance(message, dict) and 'id' in message:
            replies.put(message)


class Session:
    def __init__(self, program, timeout, home):
        environment = dict(os.environ, HOME=home, XDG_CONFIG_HOME=home, XDG_DATA_HOME=home, APPDATA=home,
                           GUI_FORMS_MCP_ATTACH='0')
        self.process = subprocess.Popen([program, '--mcp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, env=environment)
        self.replies = queue.Queue()
        self.timeout = timeout
        self.next_id = 1
        threading.Thread(target=reader, args=(self.process.stdout, self.replies), daemon=True).start()

    def request(self, method, params):
        identity = self.next_id
        self.next_id += 1
        self.process.stdin.write((json.dumps({'jsonrpc': '2.0', 'id': identity, 'method': method,
                                              'params': params}) + '\n').encode('utf-8'))
        self.process.stdin.flush()
        while True:
            message = self.replies.get(timeout=self.timeout)
            if message.get('id') == identity:
                return message

    def call(self, tool, arguments):
        reply = self.request('tools/call', {'name': tool, 'arguments': arguments})
        result = reply.get('result', {})
        text = ''.join(item.get('text', '') for item in result.get('content', []) if item.get('type') == 'text')
        return not result.get('isError', False) and 'result' in reply, text


def undocumented(tools):
    # The program's tools, or arguments of them, with no description.
    missing = []
    for tool in tools:
        name = tool.get('name', '')
        if not (name.startswith('plan_paint_') or name.startswith('command_')):
            continue
        if not tool.get('description', '').strip():
            missing.append(name)
        properties = tool.get('inputSchema', {}).get('properties', {})
        for argument, schema in properties.items():
            if not schema.get('description', '').strip():
                missing.append(name + '.' + argument)
    return missing


def seed_recovery(home):
    # An unfinished-artwork snapshot where Plan Paint looks for one: only
    # its presence matters, for the launch must not open it.
    if sys.platform == 'darwin':
        folder = os.path.join(home, 'Library', 'Application Support', 'rainstar', 'RainstarPaint')
    else:
        folder = os.path.join(home, 'rainstar', 'RainstarPaint')
    snapshot = os.path.join(folder, 'Recovery', '0f0e0d0c-0b0a-4908-8706-050403020100')
    os.makedirs(snapshot)
    with open(os.path.join(snapshot, '0.rpr'), 'wb') as record:
        record.write(b'not a picture')


def launch_without_prompt(program, timeout, check):
    with tempfile.TemporaryDirectory() as home:
        seed_recovery(home)
        session = Session(program, timeout, home)
        try:
            initialized = session.request('initialize', {'protocolVersion': PROTOCOL_VERSION, 'capabilities': {},
                                                         'clientInfo': {'name': 'plan-paint-launch',
                                                                        'version': '1'}})
            check('second launch initializes', 'result' in initialized)
            session.process.stdin.write(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
            session.process.stdin.flush()
            notice = ''
            listing = ''
            for _ in range(20):
                ok, listing = session.call('windows', {})
                ok, notice = session.call('snapshot', {})
                if 'can be recovered' in notice:
                    break
                time.sleep(0.25)
            check('launch opens no dialog', ok and 'dialog' not in listing.lower(), listing)
            check('recovery is offered in the status bar', 'can be recovered' in notice, notice[-400:])
        except queue.Empty:
            check('second launch replies', False, 'no reply within %s seconds' % timeout)
        except (BrokenPipeError, OSError) as error:
            check('second launch transport', False, str(error))
        try:
            session.process.stdin.close()
        except OSError:
            pass
        try:
            code = session.process.wait(timeout=timeout)
            check('end of input closes an unchanged picture', code == 0, 'exit %s' % code)
        except subprocess.TimeoutExpired:
            session.process.kill()
            session.process.wait()
            check('end of input closes an unchanged picture', False, 'still running')


def colour_at(text, label):
    for line in text.splitlines():
        if line.startswith(label + ' '):
            return line.split(' ', 1)[1].strip()
    return ''


def main(argv):
    if len(argv) < 2:
        print('usage: model_tools_mcp.py <plan-paint> [timeout seconds]', file=sys.stderr)
        return 2
    if sys.platform.startswith('linux') and not os.environ.get('DISPLAY') and not os.environ.get('WAYLAND_DISPLAY'):
        print('skipped: no window server')
        return 77
    timeout = float(argv[2]) if len(argv) > 2 else 30.0
    failures = 0
    with tempfile.TemporaryDirectory() as home:
        session = Session(argv[1], timeout, home)

        def check(name, condition, detail=''):
            nonlocal failures
            print(name, 'ok' if condition else 'FAILED', detail if not condition else '')
            failures += 0 if condition else 1

        try:
            initialized = session.request('initialize', {'protocolVersion': PROTOCOL_VERSION, 'capabilities': {},
                                                         'clientInfo': {'name': 'plan-paint-model-tools',
                                                                        'version': '1'}})
            check('initialize', 'result' in initialized)
            session.process.stdin.write(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
            session.process.stdin.flush()
            listed = session.request('tools/list', {})
            names = {tool.get('name') for tool in listed.get('result', {}).get('tools', [])}
            wanted = {'plan_paint_path', 'plan_paint_fill', 'plan_paint_gradient', 'plan_paint_text',
                      'plan_paint_sample', 'plan_paint_draw'}
            check('tools/list', wanted <= names, sorted(wanted - names))
            missing = undocumented(listed.get('result', {}).get('tools', []))
            check('every program tool and argument is described', not missing, missing)
            ok, text = session.call('plan_paint_new_image', {'width': 200, 'height': 120})
            check('new image', ok, text)
            ok, text = session.call('plan_paint_draw', {'shape': 'rectangle', 'from': {'x': 20, 'y': 20},
                                                        'to': {'x': 60, 'y': 40}, 'fill': '#FF0000',
                                                        'colour': 'none', 'rotation': 90})
            check('rotated rectangle', ok and 'turned 90 degrees' in text, text)
            ok, text = session.call('plan_paint_sample', {'points': [{'x': 40, 'y': 12}, {'x': 22, 'y': 30}]})
            # Turned a quarter about (40, 30), the 40 by 20 box stands 20 wide and 40 tall.
            check('rotation sampled', ok and colour_at(text, '40.0,12.0') == '#FF0000' and
                  colour_at(text, '22.0,30.0') == '#FFFFFF', text)
            # A second drawing is an effect too (1.0.5 review: it replied
            # "effect: none").
            ok, text = session.call('plan_paint_draw', {'shape': 'rectangle', 'from': {'x': 150, 'y': 10},
                                                        'to': {'x': 170, 'y': 20},
                                                        'fill': '#00FFFF', 'colour': 'none'})
            check('second draw has an effect', ok and 'effect: none' not in text, text)
            ok, text = session.call('plan_paint_sample', {'points': [{'x': 160, 'y': 15}]})
            check('second drawing sampled', ok and colour_at(text, '160.0,15.0') == '#00FFFF', text)
            # [x, y] pairs work as {x, y} does, in the schemas too (round 5).
            ok, text = session.call('plan_paint_draw', {'shape': 'rectangle', 'from': [176, 10], 'to': [186, 20],
                                                        'fill': '#FF00FF', 'colour': 'none'})
            check('pair points draw', ok, text)
            ok, text = session.call('plan_paint_sample', {'points': [[181, 15]]})
            check('pair points sampled', ok and colour_at(text, '181.0,15.0') == '#FF00FF', text)
            schemas = {tool.get('name'): tool.get('inputSchema', {})
                       for tool in listed.get('result', {}).get('tools', [])}
            from_type = schemas.get('plan_paint_draw', {}).get('properties', {}).get('from', {}).get('type')
            check('point schemas take both forms', from_type == ['object', 'array'], repr(from_type))
            ok, text = session.call('plan_paint_path', {'points': [
                {'x': 100, 'y': 20}, {'x': 140, 'y': 60, 'c1': {'x': 130, 'y': 20}, 'c2': {'x': 140, 'y': 40}},
                {'x': 100, 'y': 60}], 'fill': '#0000FF', 'colour': 'none'})
            check('bezier path', ok, text)
            ok, text = session.call('plan_paint_sample', {'points': [{'x': 110, 'y': 50}]})
            check('path sampled', ok and colour_at(text, '110.0,50.0') == '#0000FF', text)
            ok, text = session.call('plan_paint_fill', {'at': {'x': 5, 'y': 110}, 'colour': '#00FF00'})
            check('bucket fill', ok, text)
            ok, text = session.call('plan_paint_sample', {'points': [{'x': 190, 'y': 5}]})
            check('fill sampled', ok and colour_at(text, '190.0,5.0') == '#00FF00', text)
            ok, text = session.call('plan_paint_gradient', {'from': {'x': 150, 'y': 80}, 'to': {'x': 190, 'y': 110},
                                                            'stops': [{'position': 0, 'colour': '#000000'},
                                                                      {'position': 1, 'colour': '#FFFFFF'}]})
            check('gradient', ok, text)
            ok, text = session.call('plan_paint_sample', {'points': [{'x': 151, 'y': 95}, {'x': 189, 'y': 95}]})
            check('gradient sampled', ok and colour_at(text, '151.0,95.0') < colour_at(text, '189.0,95.0'), text)
            ok, text = session.call('plan_paint_gradient', {'from': {'x': 100, 'y': 80}, 'to': {'x': 140, 'y': 110},
                                                            'kind': 'radial', 'radius': 10,
                                                            'stops': [{'position': 0, 'colour': '#000000'},
                                                                      {'position': 1, 'colour': '#FFFFFF'}]})
            check('radial gradient radius', ok and 'radius 10 px' in text, text)
            ok, text = session.call('plan_paint_sample', {'points': [{'x': 120, 'y': 95}, {'x': 133, 'y': 95}]})
            check('radial radius sampled', ok and colour_at(text, '133.0,95.0') == '#FFFFFF' and
                  colour_at(text, '120.0,95.0') != '#FFFFFF', text)
            ok, text = session.call('plan_paint_text', {'at': {'x': 10, 'y': 70}, 'text': 'koi', 'size': 30,
                                                        'colour': '#000000'})
            check('text', ok and re.search(r', \d+ x \d+ px', text) is not None, text)
            ok, text = session.call('plan_paint_text', {'at': {'x': 10, 'y': 70}, 'text': 'koi', 'size': 30,
                                                        'measure': True})
            check('text measure', ok and 'nothing drawn' in text and re.search(r'\d+ x \d+ px', text), text)
            steps = [{'tool': 'plan_paint_stroke', 'points': [{'x': 10 + index, 'y': 112},
                                                              {'x': 10 + index, 'y': 118}],
                      'colour': '#123456', 'size': 1} for index in range(100)]
            ok, text = session.call('batch', {'steps': steps})
            check('100-step batch', ok and '100 steps' in text, text[-200:])
            # The Resize dialog: OK is marked default, and pressing it says what it did.
            ok, text = session.call('command_resize', {})
            check('resize dialog opens', ok, text)
            ok, text = session.call('snapshot', {})
            ok_line = next((line for line in text.splitlines() if '[dialog-ok]' in line), '')
            check('dialog OK is marked default', ok and ' default' in ok_line, ok_line or text[-300:])
            # A model never submits a dialog (round 1): pressing OK is refused,
            # and it closes the one it opened.
            ok, text = session.call('act', {'action': 'press', 'target': 'dialog-ok'})
            check('pressing a dialog\'s OK is refused', not ok, text)
            ok, listing = session.call('windows', {})
            dialog = re.search(r'\[([a-z0-9.-]*dialog[a-z0-9.-]*)\]', listing)
            ok, text = session.call('act', {'action': 'close', 'target': dialog.group(1) if dialog else 'dialog'})
            check('the model closes the dialog it opened', ok, listing + text)
            ok, text = session.call('capture', {'points': [[1, 1], {'x': 2, 'y': 2}]})
            check('capture points, both shapes', ok and '1,1 #' in text and '2,2 #' in text, text)
            ok, text = session.call('plan_paint_image_info', {})
            check('image info', ok and 'colours: primary' in text, text)
            # The end of the input is a hard disconnect with the default:
            # the picture's save question would be the user's, so this
            # session discards it itself.
            ok, text = session.call('disconnect', {'mode': 'hard', 'discard_unsaved': True})
            check('hard disconnect discarding', ok, text)
        except queue.Empty:
            check('replies', False, 'no reply within %s seconds' % timeout)
        except (BrokenPipeError, OSError) as error:
            check('transport', False, str(error))
        try:
            session.process.stdin.close()
        except OSError:
            pass
        try:
            code = session.process.wait(timeout=timeout)
            check('shutdown', code == 0, 'exit %s' % code)
        except subprocess.TimeoutExpired:
            session.process.kill()
            session.process.wait()
            check('shutdown', False, 'still running')
        if failures:
            sys.stderr.write(session.process.stderr.read().decode('utf-8', 'replace')[-2000:])
        launch_without_prompt(argv[1], timeout, check)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
