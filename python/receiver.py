
"""Mac-side UDP receiver for ESP32 Wi-Fi Air Mouse.
Run on a trusted LAN only. Live mode requires macOS Accessibility permission.
Default mode prints commands without moving the mouse.
"""
import argparse
import os
import socket
import time

MOUSE_MULTIPLIER = 5  # Change to 7 or 10 for faster cursor movement


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--port', type=int, default=44771)
    p.add_argument('--esp-ip', required=True)
    p.add_argument('--live', action='store_true',
                   help='Enable real mouse/keyboard actions (default: dry-run)')
    a = p.parse_args()

    token = os.getenv('WAND_SHARED_TOKEN')
    if not token or token == 'REPLACE_WITH_RANDOM_SECRET':
        p.error('Set a strong WAND_SHARED_TOKEN environment variable')

    if a.live:
        import pyautogui
        pyautogui.FAILSAFE = True
        pyautogui.PAUSE = 0

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', a.port))
    sock.settimeout(1)

    print(
        f'Listening on UDP :{a.port}; '
        f'allowlisted ESP32: {a.esp_ip}; '
        f'dry run: {not a.live}'
    )

    last_seq = -1
    last_session = None
    retired_sessions = set()
    last_event = 0.0
    move_last = 0.0

    try:
        while True:
            try:
                data, (host, _) = sock.recvfrom(512)
            except socket.timeout:
                continue

            if host != a.esp_ip or len(data) > 256:
                continue

            try:
                fields = data.decode('ascii').strip().split('|')
                if len(fields) != 6:
                    continue
                received, session, seq, command, dx, dy = fields

                if received != token:
                    continue
                if len(session) != 8 or any(c not in '0123456789abcdef' for c in session):
                    continue

                seq = int(seq)
                dx, dy = int(dx), int(dy)
                if seq <= 0 or seq > 0xFFFFFFFF:
                    continue
                if session != last_session:
                    if session in retired_sessions:
                        continue
                    # Accept a new boot session and reject delayed packets from old ones.
                    if last_session is not None:
                        retired_sessions.add(last_session)
                    last_session = session
                    last_seq = -1
                    print(f'ESP32 session: {session}')
                if seq <= last_seq:
                    continue

                if command not in {
                    'MOVE', 'CENTER', 'LEFT_CLICK', 'RIGHT_CLICK',
                    'SCROLL_DOWN', 'SCROLL_UP', 'NEXT_TAB', 'PREV_TAB'
                }:
                    continue

                if abs(dx) > 600 or abs(dy) > 600:
                    continue

            except (ValueError, UnicodeError):
                continue

            last_seq = seq
            now = time.monotonic()

            if command == 'MOVE':
                if now - move_last < 0.015:
                    continue
                move_last = now
            else:
                if now - last_event < 0.35:
                    continue
                last_event = now

            print(f'{command} {dx} {dy}')

            if not a.live:
                continue

            try:
                if command == 'MOVE':
                    pyautogui.moveRel(
                        dx * MOUSE_MULTIPLIER,
                        dy * MOUSE_MULTIPLIER,
                        duration=0
                    )

                elif command == 'CENTER':
                    w, h = pyautogui.size()
                    pyautogui.moveTo(
                        w // 2, h // 2, duration=0.15
                    )

                elif command == 'LEFT_CLICK':
                    pyautogui.click()

                elif command == 'RIGHT_CLICK':
                    pyautogui.click(button='right')

                elif command == 'SCROLL_DOWN':
                    pyautogui.scroll(-4)

                elif command == 'SCROLL_UP':
                    pyautogui.scroll(4)

                elif command == 'NEXT_TAB':
                    pyautogui.hotkey('command', 'shift', ']')

                elif command == 'PREV_TAB':
                    pyautogui.hotkey('command', 'shift', '[')

            except pyautogui.FailSafeException:
                print('Emergency stop: mouse moved to corner.')
                break

    except KeyboardInterrupt:
        print('Receiver stopped')

    finally:
        sock.close()


if __name__ == '__main__':
    main()
