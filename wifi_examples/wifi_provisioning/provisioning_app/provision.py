#!/usr/bin/env python3
"""Send Wi-Fi credentials to the RW612 provisioning TCP server."""

import argparse
import getpass
import random
import socket
import struct
import sys

TCP_PORT = 15000
TCP_TIMEOUT_SECONDS = 30
MDNS_ADDRESS = "224.0.0.251"
MDNS_PORT = 5353
MDNS_SERVICE_ENUMERATION = "_services._dns-sd._udp.local"
DEFAULT_SSID = "IZZI-AC77"
DEFAULT_PASSWORD ="3C04610AAC77"

def send_credentials(device_ip: str, port: int, ssid: str, password: str) -> str:
    if not ssid or not password:
        raise ValueError("SSID and password cannot be empty")
    if any(character in ssid or character in password for character in "\r\n"):
        raise ValueError("SSID and password cannot contain newline characters")

    payload = f"{ssid}\n{password}\n".encode("utf-8")
    print(f"Connecting to {device_ip}:{port}...")
    try:
        connection = socket.create_connection((device_ip, port), TCP_TIMEOUT_SECONDS)
    except socket.timeout as error:
        raise TimeoutError(f"TCP connection timed out to {device_ip}:{port}") from error

    with connection:
        print(f"Connected; sending {len(payload)} bytes...")
        try:
            connection.sendall(payload)
            connection.shutdown(socket.SHUT_WR)
            print("Payload sent; waiting for board response...")
            return connection.recv(128).decode("utf-8", errors="replace").strip()
        except socket.timeout as error:
            raise TimeoutError(f"Board did not respond on TCP {device_ip}:{port}") from error


def encode_dns_name(name: str) -> bytes:
    labels = name.rstrip(".").split(".")
    return b"".join(bytes([len(label)]) + label.encode("ascii") for label in labels) + b"\0"


def read_dns_name(packet: bytes, offset: int) -> tuple[str, int]:
    labels = []
    next_offset = None
    while True:
        length = packet[offset]
        offset += 1
        if length == 0:
            return ".".join(labels), next_offset or offset
        if length & 0xC0 == 0xC0:
            pointer = ((length & 0x3F) << 8) | packet[offset]
            if next_offset is None:
                next_offset = offset + 1
            suffix, _ = read_dns_name(packet, pointer)
            labels.append(suffix)
            return ".".join(labels), next_offset
        labels.append(packet[offset:offset + length].decode("ascii"))
        offset += length


def send_mdns_query(query_name: str, query_type: int) -> list[tuple[str, int, str]]:
    transaction_id = random.randrange(1, 65536)
    query = struct.pack("!HHHHHH", transaction_id, 0, 1, 0, 0, 0)
    query += encode_dns_name(query_name) + struct.pack("!HH", query_type, 1)
    records = []

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as connection:
        connection.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        connection.setsockopt(socket.SOL_IP, socket.IP_MULTICAST_TTL, 255)
        connection.bind(("", MDNS_PORT))
        membership = struct.pack("4s4s", socket.inet_aton(MDNS_ADDRESS), socket.inet_aton("0.0.0.0"))
        connection.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
        connection.settimeout(2)
        connection.sendto(query, (MDNS_ADDRESS, MDNS_PORT))

        while True:
            try:
                packet, _ = connection.recvfrom(1500)
            except socket.timeout:
                return records
            if len(packet) < 12:
                continue
            _, flags, questions, answers, authorities, additional = struct.unpack("!HHHHHH", packet[:12])
            if not flags & 0x8000:
                continue

            offset = 12
            for _ in range(questions):
                _, offset = read_dns_name(packet, offset)
                offset += 4

            for _ in range(answers + authorities + additional):
                record_name, offset = read_dns_name(packet, offset)
                record_type, record_class, ttl, data_length = struct.unpack("!HHIH", packet[offset:offset + 10])
                offset += 10
                record_offset = offset
                record_data = packet[offset:offset + data_length]
                offset += data_length
                if record_type == 1 and data_length == 4:
                    value = socket.inet_ntoa(record_data)
                elif record_type in (12, 33):
                    value, _ = read_dns_name(packet, record_offset + (6 if record_type == 33 else 0))
                    if record_type == 33:
                        port = struct.unpack("!H", record_data[4:6])[0]
                        value = f"{port} {value}"
                else:
                    value = ""
                records.append((record_name, record_type, value))


def discover_devices() -> list[tuple[str, str, int, str]]:
    service_records = send_mdns_query(MDNS_SERVICE_ENUMERATION, 12)
    service_types = sorted({value.rstrip(".") for _, record_type, value in service_records if record_type == 12 and value})
    devices = []

    for service_type in service_types:
        instance_records = send_mdns_query(service_type, 12)
        instances = sorted({value.rstrip(".") for _, record_type, value in instance_records if record_type == 12 and value})
        for instance in instances:
            records = send_mdns_query(instance, 33)
            srv_records = [value for _, record_type, value in records if record_type == 33 and value]
            if not srv_records:
                continue
            port_text, target = srv_records[0].split(" ", 1)
            address_records = send_mdns_query(target, 1)
            addresses = [value for _, record_type, value in address_records if record_type == 1 and value]
            if addresses:
                devices.append((instance, addresses[0], int(port_text), service_type))

    return devices


def choose_device(devices: list[tuple[str, str, int, str]]) -> tuple[str, str, int, str]:
    if not devices:
        raise TimeoutError("No mDNS services were found on the local network")
    print("\nDispositivos mDNS detectados:")
    for index, (name, address, port, service_type) in enumerate(devices, 1):
        print(f"  {index}. {name} [{service_type}] -> {address}:{port}")
    while True:
        selection = input("Seleccione el dispositivo: ").strip()
        try:
            device = devices[int(selection) - 1]
        except (ValueError, IndexError):
            print("Selección inválida.")
            continue
        return device


def main() -> int:
    parser = argparse.ArgumentParser(description="Provision an RW612 Wi-Fi connection")
    parser.add_argument("--device-ip", help="Use a fixed IP instead of mDNS discovery")
    parser.add_argument("--ssid", help="SSID to configure on the SmartBulb")
    parser.add_argument("--password",help="Password to configure on the SmartBulb")
    args = parser.parse_args()

    target_ssid = args.ssid or input("SSID de la red destino: ")
    target_password = args.password or getpass.getpass("Contraseña de la red destino: ")

    try:
        if args.device_ip:
            device_name, device_ip, device_port, service_type = "manual", args.device_ip, TCP_PORT, "manual"
        else:
            device_name, device_ip, device_port, service_type = choose_device(discover_devices())
        print(f"Enviando credenciales de {target_ssid} al dispositivo {device_name} [{service_type}] ({device_ip}:{device_port})")
        response = send_credentials(device_ip, device_port, target_ssid, target_password)
    except (OSError, TimeoutError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    print(f"Board response: {response or '(no response)'}")
    return 0 if response == "OK" else 1


if __name__ == "__main__":
    raise SystemExit(main())
