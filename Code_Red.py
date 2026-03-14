#Prog assignment 5
import sys
import time
import select  # for non-blocking socket
import json    # for new, simple packets
import threading # for non-blocking network thread and GUI
import datetime
from gpiozero import LED, Button
from netifaces import interfaces, ifaddresses, AF_INET
from socket import *
from collections import deque
from luma.core.interface.serial import spi, noop
from luma.core.render import canvas
from luma.led_matrix.device import max7219
import requests

# Node-RED Web Server Configuration
NODERED_BASE_URL = "http://localhost:1880"

serial = spi(port=0, device=0, gpio=noop())
device = max7219(serial, cascaded=1, block_orientation=-90) # cascaded=1 means there is ONE 8x8 matrix. (ZotGPT)

# List to hold dictionaries: {'time': t (elapsed s), 'value': v, 'ip': ip}
graph_data = []
# List to store raw data for file logging since the last button press
log_data = []
current_master_ip = "0.0.0.0"

s = None # Placeholder for the global socket object (initialized in network_listener)
start_time = time.time() # Global reference for elapsed time calculation

# HARDWARE SETUP 
RED_LED = LED(17)
GREEN_LED = LED(27)
BLUE_LED = LED(22)
YELLOW_LED = LED(12)
WHITE_LED = LED(23)
PUSH_BUTTON = Button(24)

MASTER_LEDS = { 
    1: RED_LED, 2: GREEN_LED, 3: BLUE_LED,
}

# Mapping the specific Master IPs to their desired color codes
MASTER_IP_COLORS = {
    "192.168.0.169": '#FF0000',   # Red Master
    "192.168.0.88": '#008000',    # Green Master
    "192.168.0.220": '#0000FF'    # Blue Master
}

# Global state for LED flashing
g_led_flash_timers = {
    1: {"last_toggle": 0, "interval": 999, "state": 0},
    2: {"last_toggle": 0, "interval": 999, "state": 0},
    3: {"last_toggle": 0, "interval": 999, "state": 0}
}
g_yellow_led_off_time = 0
g_reannounce_time = 0

VERSIONNUMBER = 6
LIGHT_UPDATE_PACKET = 0
RESET_SWARM_PACKET = 1
DEFINE_SERVER_LOGGER_PACKET = 4
MYPORT = 2910
SWARMSIZE = 5

def publish_to_nodered():
    """ Sends graph data to Node-RED via HTTP POST. """
    global graph_data, start_time
    
    try:
        now = time.time() - start_time
        recent_data = [d for d in graph_data if (now - d['time']) <= 30]
        
        if not recent_data:
            return
        
        # GRAPH 1: Photocell Data
        light_data = [
            {
                "x": round(d['time'], 2),
                "y": d['value'],
                "ip": d['ip'],
                "color": get_color(d['ip'])
            }
            for d in recent_data
        ]
        
        # Send via HTTP POST (ZotGPT)
        requests.post(
            f"{NODERED_BASE_URL}/light_data",
            json={"data": light_data},
            timeout=0.5
        )
        
        # GRAPH 2: Master Duration 
        ip_durations = {}
        for i in range(1, len(recent_data)):
            dt = recent_data[i]['time'] - recent_data[i-1]['time']
            ip = recent_data[i]['ip']
            ip_durations[ip] = ip_durations.get(ip, 0) + dt
        
        if ip_durations:
            duration_data = [
                {
                    "ip": ip,
                    "duration": round(dur, 2),
                    "color": get_color(ip)
                }
                for ip, dur in ip_durations.items()
            ]
            #ZotGPT
            requests.post(
                f"{NODERED_BASE_URL}/master_duration",
                json={"data": duration_data},
                timeout=0.5
            )
            
    except requests.exceptions.RequestException as e:
        # Silently ignore if Node-RED not running
        pass
    except Exception as e:
        print(f"Error sending to Node-RED: {e}")

def update_led_matrix():
    """ Updates the 8x8 LED matrix to show photocell data trace. Each column represents 4 seconds of data from the past 32 seconds. """
    global graph_data, start_time, device
    
    try:
        now = time.time() - start_time
        
        # Get data from the last 32 seconds (8 columns × 4 seconds)
        recent_data = [d for d in graph_data if (now - d['time']) <= 32] 
        
        if not recent_data:
            device.clear()
            return
        
        # Divide the 32-second window into 8 bins (one per column)
        column_data = [0] * 8  # Will store average light value for each column
        column_counts = [0] * 8
        
        # Aggregate data into 8 time bins
        for d in recent_data:
            age = now - d['time']  # To check how old the data point is
            column_index = int(age / 4.0)  # To check which 4-second bin does it belong to
            
            if 0 <= column_index < 8:
                column_data[column_index] += d['value']
                column_counts[column_index] += 1
        
        # Calculate averages and scale to 0-7 range for LED height
        with canvas(device) as draw:
            for col in range(8):
                if column_counts[col] > 0:
                    avg_value = column_data[col] / column_counts[col]
                    # Scale from 0-4095 to 0-7
                    height = int((avg_value / 4095.0) * 7)
                    height = max(0, min(7, height))  # Clamp to 0-7
                    
                    # Draw vertical line from bottom up
                    # Column 0 = oldest data (leftmost)
                    # Column 7 = newest data (rightmost)
                    x = 7 - col  # Reverse so newest is on the right
                    
                    # Draw from bottom (y=7) up to the height
                    for y in range(7, 7 - height - 1, -1):
                        draw.point((x, y), fill="white")
                        
    except Exception as e:
        print(f"Error updating LED matrix: {e}")
    

def get_color(ip):
    """Maps a master IP to a consistent color for the graph."""
    if ip in MASTER_IP_COLORS:
        return MASTER_IP_COLORS[ip]
        
    try:
        pseudo_random = sum(map(int, ip.split('.'))) 
        # Extended color list for unknown devices
        colors = ['#1f77b4', '#ff7f0e', '#9467bd', '#8c564b', '#e377c2', '#7f7f7f']
        return colors[pseudo_random % len(colors)]
    except:
        return 'k' # Default to black

def map_light_to_interval(light_value):
    """Maps a light value to a flash interval in seconds."""
    light_value = max(0, min(4095, light_value))
    interval = (light_value - 0) * (0.1 - 1.0) / (4095 - 0) + 1.0
    return interval

def update_leds():
    """Non-blocking LED flash handler."""
    global g_yellow_led_off_time, g_led_flash_timers
    now = time.time()

    # Handle Yellow LED 3-second timer
    if g_yellow_led_off_time > 0 and now > g_yellow_led_off_time:
        YELLOW_LED.off()
        g_yellow_led_off_time = 0 
    
    # Handle R,G,B flashing
    for master_id, led in MASTER_LEDS.items():
        timer = g_led_flash_timers[master_id]
        
        if timer["interval"] >= 999: # Master off signal
            led.off()
            timer["state"] = 0
            continue 

        if (now - timer["last_toggle"]) >= timer["interval"]:
            timer["last_toggle"] = now
            if timer["state"] == 0:
                led.on()
                timer["state"] = 1
            else:
                led.off()
                timer["state"] = 0

# PACKET SENDING FUNCTIONS 

def SendDEFINE_SERVER_LOGGER_PACKET(s):
    print("DEFINE_SERVER_LOGGER_PACKET Sent")
    s.setsockopt(SOL_SOCKET, SO_BROADCAST, 1)
    myIP = ['127','0','0','1'] # Default
    try:
       for ifaceName in interfaces():
          addresses = [i['addr'] for i in ifaddresses(ifaceName).setdefault(AF_INET, [{'addr':'No IP addr'}] )]
          myIP = addresses[0].split('.')
    except Exception as e:
          print(f"Could not get IP address, using 127.0.0.1: {e}")
          print(f"My IP: {myIP}")
          data = bytearray(14)
          data[0] = 0xF0
          data[1] = DEFINE_SERVER_LOGGER_PACKET
          data[2] = 0xFF
          data[3] = VERSIONNUMBER
          data[4] = int(myIP[0])
          data[5] = int(myIP[1])
          data[6] = int(myIP[2])
          data[7] = int(myIP[3])
          data[13] = 0x0F
          s.sendto(data, ('<broadcast>', MYPORT))
    pass


def SendRESET_SWARM_PACKET(s):
    print("RESET_SWARM_PACKET Sent")
    s.setsockopt(SOL_SOCKET, SO_BROADCAST, 1)
    data = bytearray(14)
    data[0] = 0xF0
    data[1] = RESET_SWARM_PACKET
    data[2] = 0xFF
    data[3] = VERSIONNUMBER
    data[13] = 0x0F
    s.sendto(data, ('<broadcast>', MYPORT))
    pass

# BUTTON HANDLER

def handle_button_press():
    """
    Called by GPIO interrupt. Resets state, saves log, and signals local server to restart.
    """
    global g_yellow_led_off_time, g_reannounce_time, s, graph_data, log_data, current_master_ip, start_time
    
    print("--- BUTTON PRESS: Sending RESET_SWARM_PACKET ---")

    # SAVE CURRENT LOG FILE
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename = f"log_{timestamp}.txt"

    try:
        with open(filename, "w") as f:
            f.write(f"Log generated at: {timestamp}\n")
            f.write("--- Logged information since last Button Press ---\n")
            for entry in log_data:
                f.write(f"{entry}\n") 
        print(f"Saved log file: {filename}")
    except Exception as e:
        print(f"Error saving log: {e}")

    # RESET BUFFERS AND TIME
    log_data.clear()
    graph_data.clear()
    current_master_ip = "0.0.0.0" 
    start_time = time.time() # Reset the starting point for elapsed time

    # RESET NODE-RED GRAPHS via HTTP
    try:
        requests.post(f"{NODERED_BASE_URL}/reset", json={"reset": True}, timeout=1)
        print("Reset Node-RED dashboard")
    except:
        pass  # Node-RED might not be running
    
    # SEND RESET PACKET & LED
    try:
        SendRESET_SWARM_PACKET(s)
        g_reannounce_time = time.time() + 3.5  
    except Exception as e:
        print(f"Error sending reset packet: {e}")
        
    YELLOW_LED.on()
    g_yellow_led_off_time = time.time() + 3.0

# NETWORK THREAD 

def network_listener():
    """Runs continuously to listen for BINARY UDP packets."""
    global current_master_ip, graph_data, log_data, s, g_reannounce_time, start_time, g_led_flash_timers

    # Setup sockets
    s = socket(AF_INET, SOCK_DGRAM)
    s.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1) 
    host = '' 
    s.bind((host, MYPORT))

    print("--------------")
    print("LightSwarm Logger")
    print("Version ", VERSIONNUMBER)
    print("Hardware-Modded for Assignment")
    print("--------------")

    SendDEFINE_SERVER_LOGGER_PACKET(s)
    time.sleep(1)
    SendDEFINE_SERVER_LOGGER_PACKET(s)

    # Setup legacy swarmStatus array 
    swarmStatus = [[0 for x in range(6)] for x in range(SWARMSIZE)]
    for i in range(0,SWARMSIZE):
      swarmStatus[i][0] = "NP"
      swarmStatus[i][5] = 0

    PUSH_BUTTON.when_pressed = handle_button_press

    print("--- System Ready. Listening for ESPs and button presses. ---")
    print("Press the button to send a RESET command.")

    # Track the current master
    current_master_id = None

    last_publish = 0

    while True:
        try:
            readable, _, _ = select.select([s], [], [], 0.1) 
            
            if readable:
                d = s.recvfrom(1024)
                message_bytes = d[0]
                addr = d[1]
                
                # BINARY PACKET PARSING 
                # Check length 
                if len(message_bytes) < 7:
                    continue

                # Check Start Byte 
                if message_bytes[0] != 0xF0:
                    continue

                # Check Packet Type
                packet_type = message_bytes[1]
                
                if packet_type == 0: # LIGHT_UPDATE_PACKET
                    
                    # Byte 2 is the ESP ID
                    master_id = message_bytes[2]
                    
                    # Byte 3 is the Master State 
                    is_master = message_bytes[3]
                    
                    # Bytes 5 and 6 are the Light Value 
                    light_val = (message_bytes[5] << 8) + message_bytes[6]
                    
                    addr_ip = addr[0]

                    # Only process if this ESP claims to be master
                    if is_master == 1:
                        print(f"Master {master_id}@{addr_ip} reported light: {light_val}", flush=True)
                        
                        current_time = time.time() - start_time
                        
                        # Append data to global lists 
                        graph_data.append({"time": current_time, "value": light_val, "ip": addr_ip})
                        log_data.append(f"{current_time}, {addr_ip}, {light_val}")
                        current_master_ip = addr_ip
                        
                        # LED LOGIC 
                        # Check if master changed
                        if current_master_id != master_id:
                            print(f"*** MASTER CHANGED: {current_master_id} -> {master_id} ***")
                            current_master_id = master_id
                            
                            # Turn off ALL LEDs first
                            for i in [1, 2, 3]:
                                g_led_flash_timers[i]["interval"] = 999
                                MASTER_LEDS[i].off()
                        
                        # Update the current master's LED flash rate
                        if master_id in MASTER_LEDS:
                            g_led_flash_timers[master_id]["interval"] = map_light_to_interval(light_val)
                            g_led_flash_timers[master_id]["last_toggle"] = time.time()

            update_leds()
            update_led_matrix()

            # Send to Node-RED every 1 second
            now = time.time()
            if now - last_publish >= 1.0:
                publish_to_nodered()  # Uses HTTP now!
                last_publish = now
            
            # Handle scheduled re-announcements
            if g_reannounce_time > 0 and time.time() >= g_reannounce_time:
                print("Re-announcing RPi to ESPs...")
                SendDEFINE_SERVER_LOGGER_PACKET(s)
                time.sleep(0.1)
                SendDEFINE_SERVER_LOGGER_PACKET(s)
                time.sleep(0.1)
                SendDEFINE_SERVER_LOGGER_PACKET(s)
                g_reannounce_time = 0
                print("Re-announcement complete. Listening for ESPs...")
                
        except OSError as e:
            if e.errno == 9: # Socket closed
                break
        except Exception as e:
            print(f"Error in network loop: {e}")
            
    # FINAL CLEANUP
    if s is not None:
        s.close()

# MAIN EXECUTION BLOCK 

if __name__ == '__main__':
    
    print("=== LightSwarm Logger with Node-RED Dashboard ===")
    print(f"You can access the dashoard now ")
    print("Press Ctrl+C to exit")

    try:
        network_listener()
        
    except KeyboardInterrupt:
        print("\nExiting program...")
    finally:
        # Final physical cleanup for the RPi GPIO pins
        print("Cleaning up GPIO.")
        RED_LED.off()
        GREEN_LED.off()
        BLUE_LED.off()
        YELLOW_LED.off()
        WHITE_LED.off()
