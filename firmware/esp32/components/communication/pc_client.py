#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PC Client: Auto-discover ESP32 and receive sensor data
Button trigger mode: ESP32 sends notification when button is pressed
"""

import socket
import os
from datetime import datetime

# Configuration
UDP_DISCOVERY_PORT = 9999      # Port for ESP32 discovery broadcast
UDP_NOTIFICATION_PORT = 9998   # Port for receiving data-ready notifications
TCP_PORT = 8888                # TCP port for data request
BUFFER_SIZE = 256
DATA_FILE = "sensor_data.txt"
REQUEST_CMD = "GET_DATA"
NOTIFICATION_CMD = "ESP32_DATA_READY"

def discover_esp32():
    """
    Discover ESP32 via UDP broadcast
    Returns: (esp32_ip, tcp_port) or (None, None)
    """
    print("Searching for ESP32...")

    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    udp_socket.bind(("0.0.0.0", UDP_DISCOVERY_PORT))
    udp_socket.settimeout(5)

    try:
        while True:
            data, addr = udp_socket.recvfrom(1024)
            message = data.decode('utf-8')

            if message.startswith("ESP32_HEALTH:"):
                parts = message.split(":")
                if len(parts) == 3:
                    esp32_ip = parts[1]
                    tcp_port = int(parts[2])
                    print(f"Found ESP32! IP: {esp32_ip}, TCP Port: {tcp_port}")
                    return esp32_ip, tcp_port

    except socket.timeout:
        print("Search timeout, ESP32 not found")
        return None, None
    finally:
        udp_socket.close()

def send_pc_ip_to_esp32(esp32_ip, tcp_port):
    """
    Send PC IP to ESP32 so it can notify us when button is pressed
    """
    try:
        tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_socket.settimeout(5)
        tcp_socket.connect((esp32_ip, tcp_port))
        
        # Send PC IP and notification port
        pc_ip = socket.gethostbyname(socket.gethostname())
        msg = f"PC_IP:{pc_ip}:{UDP_NOTIFICATION_PORT}"
        tcp_socket.send(msg.encode('utf-8'))
        tcp_socket.close()
        print(f"Sent PC IP to ESP32: {pc_ip}")
        return True
    except Exception as e:
        print(f"Failed to send PC IP: {e}")
        return False

def request_data(esp32_ip, tcp_port):
    """
    Connect to ESP32 and request data
    """
    try:
        tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_socket.settimeout(5)
        tcp_socket.connect((esp32_ip, tcp_port))

        # Send request
        tcp_socket.send(REQUEST_CMD.encode('utf-8'))

        # Receive response
        data = tcp_socket.recv(BUFFER_SIZE)
        tcp_socket.close()

        if not data:
            print("No response from ESP32")
            return False

        # Parse response
        message = data.decode('utf-8').strip()
        parts = message.split(',')

        if len(parts) == 4:
            body_temp, external_temp, timestamp, heart_rate = parts
            
            output = f"[{timestamp}] Body Temp: {body_temp}C | External Temp: {external_temp}C | Heart Rate: {heart_rate} bpm"
            print(f"\nReceived Data:\n{output}")

            # Save to file
            with open(DATA_FILE, 'a', encoding='utf-8') as f:
                f.write(f"{timestamp},{body_temp},{external_temp},{heart_rate}\n")

            print(f"Data saved to: {os.path.abspath(DATA_FILE)}")
            return True
        else:
            print(f"Invalid data format: {message}")

    except Exception as e:
        print(f"Error requesting data: {e}")

    return False

def wait_for_notification(esp32_ip, tcp_port):
    """
    Wait for ESP32 button press notification
    """
    print(f"\nWaiting for button press notification on UDP port {UDP_NOTIFICATION_PORT}...")
    print("(Press Ctrl+C to exit)")

    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp_socket.bind(("0.0.0.0", UDP_NOTIFICATION_PORT))
    udp_socket.settimeout(1)  # 1 second timeout for checking interrupt

    try:
        while True:
            try:
                data, addr = udp_socket.recvfrom(1024)
                message = data.decode('utf-8')

                if message == NOTIFICATION_CMD:
                    print(f"\nButton pressed detected! Requesting data...")
                    request_data(esp32_ip, tcp_port)
                    print("\nWaiting for next button press...")
                    
            except socket.timeout:
                continue
                
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        udp_socket.close()

def main():
    print("="*50)
    print("ESP32 Health Data Client - Button Trigger Mode")
    print("="*50)

    # Step 1: Discover ESP32 (or use manual IP if discovery fails)
    esp32_ip, tcp_port = discover_esp32()

    if esp32_ip is None:
        print("\nAuto-discovery failed, using manual IP...")
        # Manual configuration - change this to your ESP32 IP
        esp32_ip = "145.76.19.112"  # <-- Change this to your ESP32 IP
        tcp_port = 8888
        print(f"Using manual IP: {esp32_ip}:{tcp_port}")

    # Step 2: Send PC IP to ESP32
    send_pc_ip_to_esp32(esp32_ip, tcp_port)

    # Step 3: Wait for button press notifications
    wait_for_notification(esp32_ip, tcp_port)

    print("\nProgram ended")

if __name__ == "__main__":
    main()
