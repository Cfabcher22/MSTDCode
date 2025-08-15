import serial
import time

# Set this to the COM port your GIGA appears as in Device Manager
PORT = "COM5"     # Replace with your actual COM port (e.g., COM4, COM5)
BAUD = 115200     # Must match Arduino baud rate

try:
    print(f"Connecting to {PORT} at {BAUD} baud...")
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(2)  # Allow time for Arduino reset
    print("Connected! Listening for messages...\n")

    while True:
        line = ser.readline().decode("utf-8").strip()
        if line:
            print("Received:", line)

except serial.SerialException as e:
    print("Serial connection failed:", e)
except Exception as e:
    print("Error:", e)
