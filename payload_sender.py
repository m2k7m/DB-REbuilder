import os
import socket


def send_payload(payload_path: str, host: str, port: int):
    if not os.path.exists(payload_path):
        print(f"Error: File '{payload_path}' not found.")
        return

    print(f"Preparing to send {payload_path} to {host}:{port}...")

    try:
        with open(payload_path, "rb") as f:
            data = f.read()

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        sock.settimeout(10)
        sock.connect((host, port))

        sock.sendall(data)
        sock.close()
        print(f"Success! Sent {len(data)} bytes to {host}:{port}")

    except ConnectionRefusedError:
        print("\n[!] Connection Refused (Error 111):")
        print(f"The device at {host} is not listening on port {port}.")
        print("Make sure the Payloader server is running on the PS4 before sending.")
    except TimeoutError:
        print("\n[!] Connection Timed Out.")
        print(f"Check if IP {host} is correct and the device is turned on.")
    except Exception as e:
        print(f"\n[!] An error occurred: {e}")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Send payload to PS4")
    parser.add_argument("--host", type=str, help="PS4 IP address")
    parser.add_argument("--port", type=int, default=9090, help="PS4 port")
    args = parser.parse_args()

    host = args.host
    port = args.port

    if not host:
        host = input("Enter the PS4 IP address: ")

    files = [f for f in os.listdir(".") if f.endswith((".elf", ".bin"))]

    if not files:
        print("No .elf or .bin files found in the current directory.")
    else:
        if len(files) == 1:
            print(f"Found one payload: {files[0]}")
            print(f"Automatically sending {files[0]} to {host}:{port}...")
            send_payload(files[0], host, port)
        else:
            print("\n--- Available Payloads ---")
            for i, file in enumerate(files, 1):
                print(f"{i}. {file}")

            try:
                selection = input("\nEnter the number of the file to send: ")
                index = int(selection) - 1

                if 0 <= index < len(files):
                    selected_file = files[index]
                    send_payload(selected_file, host, port)
                else:
                    print("Invalid number selected.")
            except ValueError:
                print("Please enter a valid number.")
