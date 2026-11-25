"""Plan Paint's model tools over MCP (GUI.Forms round 4 of the pilot).

Starts Plan Paint with --mcp in a scratch home, then over MCP's stdio
framing: a new 200 by 120 picture; a filled, rotated rectangle; a filled
Bezier path; a bucket fill; a gradient; text; plan_paint_sample and capture
points reading the colours back; the Resize dialog, its OK marked default
and pressed; a 100-step batch of strokes. Each drawing is checked by
sampling. Closes the input and expects exit 0 within the
timeout. Exit 77 skips where no window server is reachable.

    python3 -B tests/model_tools_mcp.py <plan-paint executable> [timeout seconds]
"""

import json
import os
import queue
import subprocess
import sys
import tempfile
import threading

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
        environment = dict(os.environ, HOME=home, XDG_CONFIG_HOME=home, GUI_FORMS_MCP_ATTACH='0')
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
            # "effect: none"), and a point may be a pair.
            ok, text = session.call('plan_paint_draw', {'shape': 'rectangle', 'from': [150, 10], 'to': [170, 20],
                                                        'fill': '#00FFFF', 'colour': 'none'})
            check('second draw has an effect', ok and 'effect: none' not in text, text)
            ok, text = session.call('plan_paint_sample', {'points': [[160, 15]]})
            check('pair points sampled', ok and colour_at(text, '160.0,15.0') == '#00FFFF', text)
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
            ok, text = session.call('plan_paint_text', {'at': {'x': 10, 'y': 70}, 'text': 'koi', 'size': 30,
                                                        'colour': '#000000'})
            check('text', ok, text)
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
            ok, text = session.call('act', {'action': 'press', 'target': 'dialog-ok'})
            check('dialog OK submits and says so', ok and ('gave way' in text or 'closed:' in text), text)
            ok, text = session.call('capture', {'points': [[1, 1], {'x': 2, 'y': 2}]})
            check('capture points, both shapes', ok and '1,1 #' in text and '2,2 #' in text, text)
            ok, text = session.call('plan_paint_image_info', {})
            check('image info', ok and 'colours: primary' in text, text)
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
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
