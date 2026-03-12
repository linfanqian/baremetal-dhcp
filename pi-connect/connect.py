import serial
import sys
import time

SERIAL_PORT = '/dev/ttyUSB0'
BAUD_RATE = 115200

def main():
    print(f"[Client] Connecting to {SERIAL_PORT}...")
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print("[Client] Serial port opened")
    except Exception as e:
        print(f"[Client] Error: {e}")
        sys.exit(1)
   
    try:
        while True:
            # Read from serial
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                sys.stdout.write(data.decode('utf-8', errors='ignore'))
                sys.stdout.flush()
            
            # TODO: Add keyboard input handling here if needed
            time.sleep(0.01)
            
    except KeyboardInterrupt:
        print("\n[Client] Exiting...")
        ser.close()

if __name__ == "__main__":
    main()
