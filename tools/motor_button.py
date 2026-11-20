"""Inject motor-board button events through USB CDC; requires pyserial."""
import argparse
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("button", type=str.upper, choices=["BTN1", "BTN2", "CONFIG", "CONFIRM"])
    parser.add_argument("action", type=str.upper, choices=["CLICK", "LONG", "DOUBLE"])
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()
    if args.repeat < 1 or args.interval < 0:
        parser.error("repeat must be positive; interval must be nonnegative")
    with serial.Serial(args.port, 115200, timeout=8, write_timeout=3) as port:
        port.write(b"PING\n")
        if port.readline() != b"PONG MOTOR\n":
            raise RuntimeError("Selected port is not the motor controller")
        action = "CLICK" if args.action == "DOUBLE" else args.action
        count = args.repeat * (2 if args.action == "DOUBLE" else 1)
        for index in range(count):
            if index:
                time.sleep(args.interval)
            port.write(f"BTN {args.button} {action}\n".encode())
            reply = port.readline().decode().strip()
            print(reply)
            if not reply.startswith("OK BTN "):
                raise RuntimeError(reply or "No reply")
        port.write(b"MODE\n")
        print(port.readline().decode().strip())


if __name__ == "__main__":
    main()
